#ifndef PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_
#define PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_

#include "absl/status/status.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/control_channel.h"

namespace peregrine::cluster {

// Symmetric multi-instance cluster node engine executing the 4-phase lifecycle:
//   Phase 1 (Init):        Create 1..P Transport instances, register memory,
//                          pre-generate outbound Request batches, and emit
//                          PEREGRINE_NODE_READY.
//   Phase 2 (WaitStart):   Wait for "START" on `channel`.
//   Phase 3 (RunWorkload): Start CpuTimer & periodic metrics sampler. If
//                          outbound streams exist, dispatch PostAsync() across
//                          all streams, wait for completion, and emit
//                          PEREGRINE_WORKLOAD_DONE.
//   Phase 4 (WaitStop):    Wait for "STOP" on `channel`, stop sampler, emit
//                          PEREGRINE_NODE_DONE, and tear down cleanly.
class ClusterNode {
 public:
  static absl::Status Run(const NodeConfig& config, ControlChannel& channel);
};

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_CLUSTER_NODE_H_
