#ifndef PEREGRINE_TEST_CLUSTER_NODE_CONFIG_H_
#define PEREGRINE_TEST_CLUSTER_NODE_CONFIG_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/test/workloads/workload_generator.h"

namespace peregrine::cluster {

// Supported cluster traffic routing patterns.
enum class TrafficPattern {
  kAllToAll,
  kRoundRobin,
};

// Remote Peregrine instance target endpoint and registered buffer address.
struct PeerTarget {
  std::string endpoint;
  uint64_t raddr = 0;

  friend bool operator==(const PeerTarget& a, const PeerTarget& b) {
    return a.endpoint == b.endpoint && a.raddr == b.raddr;
  }
};

// Configuration for a single cluster node process (running 1..P instances).
struct NodeConfig {
  std::string ip = "127.0.0.1";
  TransportType transport_type = TransportType::kTcp;
  int num_instances = 1;
  int node_index = 0;
  uint16_t base_control_port = 10000;
  int num_conns = 1;
  workloads::WorkloadType workload = workloads::WorkloadType::kSerialFixedWrite;
  // Optional pre-constructed workload generator (used in flag-free unit tests;
  // when null, ClusterNode instantiates from `workload` via CreateWorkload()).
  std::shared_ptr<const workloads::WorkloadGenerator> workload_generator =
      nullptr;
  std::vector<PeerTarget> targets;
  TrafficPattern traffic_pattern = TrafficPattern::kAllToAll;
  bool exclude_self = false;
  uint32_t num_xfers = 1;
  bool share_buffer = true;
  absl::Duration metrics_interval = absl::Seconds(1);
};

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_CONFIG_H_
