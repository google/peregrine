#include "peregrine/test/cluster/node/cluster_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/test/cluster/node/affinity.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/control_channel.h"
#include "peregrine/test/cluster/node/instance.h"
#include "peregrine/test/cluster/node/metrics.h"
#include "peregrine/test/cluster/node/topology.h"
#include "peregrine/test/workloads/workload_generator.h"
#include "peregrine/test/workloads/workload_util.h"

namespace peregrine::cluster {
namespace {

struct OutboundStream {
  NodeInstance* instance = nullptr;
  std::string peer_endpoint;
  std::vector<Request> requests;
  uint32_t total_xfers = 1;
  uint32_t completed_xfers = 0;
};

std::shared_ptr<const workloads::WorkloadGenerator> ResolveWorkload(
    const NodeConfig& config) {
  if (config.workload_generator != nullptr) {
    return config.workload_generator;
  }
  return workloads::CreateWorkload(config.workload);
}

std::vector<std::string> CollectLocalEndpoints(
    absl::Span<const std::unique_ptr<NodeInstance>> instances) {
  std::vector<std::string> endpoints;
  endpoints.reserve(instances.size());
  for (const auto& inst : instances) {
    endpoints.push_back(inst->ControlEndpoint());
  }
  return endpoints;
}

std::vector<PeerTarget> CollectLocalTargets(
    absl::Span<const std::unique_ptr<NodeInstance>> instances) {
  std::vector<PeerTarget> targets;
  targets.reserve(instances.size());
  for (const auto& inst : instances) {
    targets.push_back(PeerTarget{
        .endpoint = inst->ControlEndpoint(),
        .raddr = inst->BufferAddr(),
    });
  }
  return targets;
}

std::vector<std::unique_ptr<OutboundStream>> BuildOutboundStreams(
    const NodeConfig& config, const workloads::WorkloadGenerator& workload_gen,
    absl::Span<const std::unique_ptr<NodeInstance>> instances,
    absl::Span<const std::string> local_endpoints) {
  const std::vector<std::vector<PeerTarget>> target_assignments =
      BuildTargetAssignments(config.node_index, config.num_instances,
                             config.targets, config.traffic_pattern,
                             config.exclude_self, local_endpoints);
  const uint32_t num_xfers = std::max<uint32_t>(1, config.num_xfers);

  std::vector<std::unique_ptr<OutboundStream>> streams;
  for (size_t i = 0; i < instances.size(); ++i) {
    for (const PeerTarget& target : target_assignments[i]) {
      auto stream = std::make_unique<OutboundStream>();
      stream->instance = instances[i].get();
      stream->peer_endpoint = target.endpoint;
      stream->requests = workload_gen.GenerateRequests(
          instances[i]->DataPtr(), reinterpret_cast<Byte*>(target.raddr));
      stream->total_xfers = num_xfers;
      streams.push_back(std::move(stream));
    }
  }
  return streams;
}

void ExecuteOutboundStreams(
    absl::Span<const std::unique_ptr<OutboundStream>> streams) {
  if (streams.empty()) {
    return;
  }
  absl::Mutex mu;
  std::deque<OutboundStream*> ready_queue;
  size_t active_streams = streams.size();

  for (const auto& stream : streams) {
    ready_queue.push_back(stream.get());
  }

  while (true) {
    OutboundStream* stream = nullptr;
    {
      absl::MutexLock lock(mu);
      auto has_work_or_done = [&]() {
        return !ready_queue.empty() || active_streams == 0;
      };
      mu.Await(absl::Condition(&has_work_or_done));
      if (ready_queue.empty()) {
        break;
      }
      stream = ready_queue.front();
      ready_queue.pop_front();
    }

    auto handle_or = stream->instance->PostAsync(
        stream->peer_endpoint, stream->requests,
        [stream, &mu, &ready_queue, &active_streams](Status s) {
          absl::MutexLock lock(mu);
          if (s == Status::kSuccess &&
              ++stream->completed_xfers < stream->total_xfers) {
            ready_queue.push_back(stream);
          } else {
            --active_streams;
          }
        });
    if (!handle_or.ok()) {
      absl::MutexLock lock(mu);
      --active_streams;
    }
  }
}

NodeMetricsSnapshot CaptureSnapshot(
    absl::Span<const std::unique_ptr<NodeInstance>> instances,
    uint64_t xfer_size_bytes, const CpuTimer& cpu_timer) {
  NodeMetricsSnapshot snap;
  snap.num_instances = static_cast<int>(instances.size());
  snap.xfer_size_bytes = xfer_size_bytes;
  snap.cpu = cpu_timer.Snapshot();
  for (const auto& inst : instances) {
    MergeTransportMetrics(snap.transport, inst->GetTransportMetrics());
  }
  return snap;
}

// RAII background thread that periodically emits `NodeMetricsSnapshot` samples.
class ScopedMetricsSampler {
 public:
  ScopedMetricsSampler(absl::Duration interval, ControlChannel& channel,
                       std::function<NodeMetricsSnapshot()> capture_fn)
      : interval_(interval),
        channel_(channel),
        capture_fn_(std::move(capture_fn)) {
    if (interval_ > absl::ZeroDuration()) {
      thread_ = util::Thread([this]() {
        while (!stop_.WaitForNotificationWithTimeout(interval_)) {
          channel_.EmitMetricsSample(capture_fn_());
        }
      });
    }
  }

  ~ScopedMetricsSampler() { Stop(); }

  void Stop() {
    if (!stop_.HasBeenNotified()) {
      stop_.Notify();
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  absl::Duration interval_;
  ControlChannel& channel_;
  std::function<NodeMetricsSnapshot()> capture_fn_;
  absl::Notification stop_;
  util::Thread thread_;
};

}  // namespace

absl::Status ClusterNode::Run(const NodeConfig& config,
                              ControlChannel& channel) {
  // Phase 1: Apply CPU affinity (if configured) before first-touch buffer
  // allocation and Transport worker thread creation.
  if (absl::Status s = ApplyCpuAffinity(config.cpu_affinity); !s.ok()) {
    return s;
  }

  const std::shared_ptr<const workloads::WorkloadGenerator> workload_gen =
      ResolveWorkload(config);
  const uint64_t xfer_size =
      std::max<uint64_t>(1, workload_gen->TotalSizeBytes());

  std::unique_ptr<RegisteredBuffer> shared_buf;
  if (config.share_buffer) {
    shared_buf = std::make_unique<RegisteredBuffer>(xfer_size);
  }

  auto instances_or = CreateInstances(config, shared_buf.get(), xfer_size);
  if (!instances_or.ok()) {
    return instances_or.status();
  }
  std::vector<std::unique_ptr<NodeInstance>> instances =
      std::move(*instances_or);

  const std::vector<std::string> local_endpoints =
      CollectLocalEndpoints(instances);
  const std::vector<PeerTarget> local_targets = CollectLocalTargets(instances);
  std::vector<std::unique_ptr<OutboundStream>> streams =
      BuildOutboundStreams(config, *workload_gen, instances, local_endpoints);

  channel.EmitNodeReady(local_targets);

  // Phase 2: Wait for synchronized START signal.
  if (!channel.WaitForCommand(kStartCommand)) {
    DestroyInstances(instances);
    return absl::CancelledError("Received EOF while waiting for START command");
  }

  // Phase 3: Execute outbound workload streams (if any) + periodic sampling.
  CpuTimer cpu_timer;
  auto snapshot_fn = [&]() {
    return CaptureSnapshot(instances, xfer_size, cpu_timer);
  };
  ScopedMetricsSampler sampler(config.metrics_interval, channel, snapshot_fn);

  if (!streams.empty()) {
    ExecuteOutboundStreams(streams);
    channel.EmitWorkloadDone(snapshot_fn());
  }

  // Phase 4: Wait for STOP signal before tearing down Transport listeners.
  channel.WaitForCommand(kStopCommand);
  sampler.Stop();
  channel.EmitNodeDone(snapshot_fn());

  DestroyInstances(instances);
  return absl::OkStatus();
}

}  // namespace peregrine::cluster
