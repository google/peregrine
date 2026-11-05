#ifndef PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_
#define PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/control_channel.h"
#include "peregrine/test/cluster/node/instance.h"
#include "peregrine/test/cluster/node/metrics.h"
#include "peregrine/test/workloads/workload_generator.h"

namespace peregrine::cluster {

struct InstanceTrace {
  std::string filename;
  std::string json;
};

// Symmetric multi-instance cluster node engine supporting both stepwise RAII
// orchestration (Create -> ResetTimer -> RunWorkload -> CollectTraces) and the
// 4-phase stdio barrier lifecycle via ClusterNode::Run:
//   Phase 1 (Init):        Create 1..P Transport instances, register memory,
//                          and emit PEREGRINE_NODE_READY.
//   Phase 2 (WaitStart):   Wait for "START" on `channel`.
//   Phase 3 (RunWorkload): Start CpuTimer & periodic metrics sampler. If
//                          outbound streams exist, dispatch PostAsync() across
//                          all streams, wait for completion, and emit
//                          PEREGRINE_WORKLOAD_DONE.
//   Phase 4 (WaitStop):    Wait for "STOP" on `channel`, stop sampler, emit
//                          PEREGRINE_NODE_DONE, and tear down cleanly.
class ClusterNode {
 public:
  using SampleCallback = ScopedMetricsSampler::SampleCallback;

  static absl::StatusOr<std::unique_ptr<ClusterNode>> Create(
      const NodeConfig& config);
  ~ClusterNode();

  ClusterNode(const ClusterNode&) = delete;
  ClusterNode& operator=(const ClusterNode&) = delete;

  absl::Span<const PeerTarget> LocalTargets() const { return local_targets_; }

  void ResetTimer();

  NodeMetricsSnapshot RunWorkload(absl::Span<const PeerTarget> targets,
                                  SampleCallback on_sample);

  NodeMetricsSnapshot CaptureMetrics() const;

  std::vector<InstanceTrace> CollectTraces() const;

  static absl::Status Run(const NodeConfig& config, ControlChannel& channel);

 private:
  ClusterNode(NodeConfig config,
              std::shared_ptr<const workloads::WorkloadGenerator> workload_gen,
              uint64_t xfer_size, std::unique_ptr<RegisteredBuffer> shared_buf,
              std::vector<std::unique_ptr<NodeInstance>> instances,
              std::vector<std::string> local_endpoints,
              std::vector<PeerTarget> local_targets);

  NodeConfig config_;
  std::shared_ptr<const workloads::WorkloadGenerator> workload_gen_;
  uint64_t xfer_size_ = 1;
  std::unique_ptr<RegisteredBuffer> shared_buf_;
  std::vector<std::unique_ptr<NodeInstance>> instances_;
  std::vector<std::string> local_endpoints_;
  std::vector<PeerTarget> local_targets_;
  CpuTimer cpu_timer_;
};

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_
