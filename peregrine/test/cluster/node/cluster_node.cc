#include "peregrine/test/cluster/node/cluster_node.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>  // NOLINT(build/c++17)
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
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
    const NodeConfig& config, absl::Span<const PeerTarget> targets,
    const workloads::WorkloadGenerator& workload_gen,
    absl::Span<const std::unique_ptr<NodeInstance>> instances,
    absl::Span<const std::string> local_endpoints) {
  const std::vector<std::vector<PeerTarget>> target_assignments =
      BuildTargetAssignments(config.node_index, config.num_instances, targets,
                             config.traffic_pattern, config.exclude_self,
                             local_endpoints);
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

absl::Status WriteTracesToDir(std::string_view trace_dir_str,
                              absl::Span<const InstanceTrace> traces) {
  if (traces.empty()) {
    return absl::OkStatus();
  }
  const std::filesystem::path trace_dir(trace_dir_str);
  std::error_code ec;
  std::filesystem::create_directories(trace_dir, ec);
  if (ec) {
    return absl::InternalError(absl::StrCat("Failed to create trace_dir '",
                                            trace_dir_str,
                                            "': ", ec.message()));
  }
  for (const InstanceTrace& trace : traces) {
    const std::filesystem::path file_path = trace_dir / trace.filename;
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    out.write(trace.json.data(),
              static_cast<std::streamsize>(trace.json.size()));
    out.close();
    if (!out) {
      return absl::InternalError(absl::StrCat(
          "Failed to write Chrome trace JSON to '", file_path.string(), "'"));
    }
  }
  return absl::OkStatus();
}

}  // namespace

ClusterNode::ClusterNode(
    NodeConfig config,
    std::shared_ptr<const workloads::WorkloadGenerator> workload_gen,
    uint64_t xfer_size, std::unique_ptr<RegisteredBuffer> shared_buf,
    std::vector<std::unique_ptr<NodeInstance>> instances,
    std::vector<std::string> local_endpoints,
    std::vector<PeerTarget> local_targets)
    : config_(std::move(config)),
      workload_gen_(std::move(workload_gen)),
      xfer_size_(xfer_size),
      shared_buf_(std::move(shared_buf)),
      instances_(std::move(instances)),
      local_endpoints_(std::move(local_endpoints)),
      local_targets_(std::move(local_targets)) {}

ClusterNode::~ClusterNode() { DestroyInstances(instances_); }

absl::StatusOr<std::unique_ptr<ClusterNode>> ClusterNode::Create(
    const NodeConfig& config) {
  // Apply CPU affinity before first-touch buffer allocation and Transport
  // worker thread creation.
  if (absl::Status s = ApplyCpuAffinity(config.cpu_affinity); !s.ok()) {
    return s;
  }

  std::shared_ptr<const workloads::WorkloadGenerator> workload_gen =
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
  std::vector<std::string> local_endpoints = CollectLocalEndpoints(instances);
  std::vector<PeerTarget> local_targets = CollectLocalTargets(instances);

  return std::unique_ptr<ClusterNode>(new ClusterNode(
      config, std::move(workload_gen), xfer_size, std::move(shared_buf),
      std::move(instances), std::move(local_endpoints),
      std::move(local_targets)));
}

void ClusterNode::ResetTimer() { cpu_timer_.Reset(); }

NodeMetricsSnapshot ClusterNode::CaptureMetrics() const {
  NodeMetricsSnapshot snap;
  snap.num_instances = static_cast<int>(instances_.size());
  snap.xfer_size_bytes = xfer_size_;
  snap.cpu = cpu_timer_.Snapshot();
  for (const auto& inst : instances_) {
    MergeTransportMetrics(snap.transport, inst->GetTransportMetrics());
  }
  return snap;
}

NodeMetricsSnapshot ClusterNode::RunWorkload(
    absl::Span<const PeerTarget> targets, SampleCallback on_sample) {
  std::vector<std::unique_ptr<OutboundStream>> streams = BuildOutboundStreams(
      config_, targets, *workload_gen_, instances_, local_endpoints_);
  if (!streams.empty()) {
    ScopedMetricsSampler sampler(
        config_.metrics_interval, [this] { return CaptureMetrics(); },
        std::move(on_sample));
    ExecuteOutboundStreams(streams);
  }
  return CaptureMetrics();
}

std::vector<InstanceTrace> ClusterNode::CollectTraces() const {
  std::vector<InstanceTrace> traces;
  if (config_.trace_every_n == 0) {
    return traces;
  }
  for (size_t i = 0; i < instances_.size(); ++i) {
    std::string json = instances_[i]->GetTransport().ExportChromeTraceJson();
    if (!absl::StrContains(json, R"("ph":"X")")) {
      continue;
    }
    traces.push_back(InstanceTrace{
        .filename =
            absl::StrFormat("node_%d_inst_%zu.json", config_.node_index, i),
        .json = std::move(json),
    });
  }
  return traces;
}

absl::Status ClusterNode::Run(const NodeConfig& config,
                              ControlChannel& channel) {
  auto node_or = Create(config);
  if (!node_or.ok()) {
    return node_or.status();
  }
  std::unique_ptr<ClusterNode> node = std::move(*node_or);

  // Pre-build outbound streams before emitting NODE_READY when targets are
  // statically supplied in config.
  std::vector<std::unique_ptr<OutboundStream>> streams =
      BuildOutboundStreams(config, config.targets, *node->workload_gen_,
                           node->instances_, node->local_endpoints_);

  channel.EmitNodeReady(node->LocalTargets());

  if (!channel.WaitForCommand(kStartCommand)) {
    return absl::CancelledError("Received EOF while waiting for START command");
  }

  node->ResetTimer();
  ScopedMetricsSampler sampler(
      config.metrics_interval, [&node] { return node->CaptureMetrics(); },
      [&channel](const NodeMetricsSnapshot& s) {
        channel.EmitMetricsSample(s);
      });

  if (!streams.empty()) {
    ExecuteOutboundStreams(streams);
    channel.EmitWorkloadDone(node->CaptureMetrics());
  }

  channel.WaitForCommand(kStopCommand);
  sampler.Stop();
  const NodeMetricsSnapshot final_snapshot = node->CaptureMetrics();
  if (config.trace_every_n > 0) {
    if (absl::Status s =
            WriteTracesToDir(config.trace_dir, node->CollectTraces());
        !s.ok()) {
      return s;
    }
  }
  channel.EmitNodeDone(final_snapshot);
  return absl::OkStatus();
}

}  // namespace peregrine::cluster
