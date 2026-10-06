#ifndef PEREGRINE_TEST_CLUSTER_NODE_FLAGS_H_
#define PEREGRINE_TEST_CLUSTER_NODE_FLAGS_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/flags/declare.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "peregrine/test/cluster/node/config.h"

ABSL_DECLARE_FLAG(std::string, ip);
ABSL_DECLARE_FLAG(int, num_instances);
ABSL_DECLARE_FLAG(int, node_index);
ABSL_DECLARE_FLAG(uint16_t, base_control_port);
ABSL_DECLARE_FLAG(std::string, transport);
ABSL_DECLARE_FLAG(int, conn);
ABSL_DECLARE_FLAG(std::string, workload);
ABSL_DECLARE_FLAG(std::string, targets);
ABSL_DECLARE_FLAG(std::string, traffic_pattern);
ABSL_DECLARE_FLAG(bool, exclude_self);
ABSL_DECLARE_FLAG(uint32_t, num_xfers);
ABSL_DECLARE_FLAG(bool, share_buffer);
ABSL_DECLARE_FLAG(absl::Duration, metrics_interval);

namespace peregrine::cluster {

// Validates that `ip` is a non-empty, non-wildcard address belonging to a
// local network interface (including loopback `127.0.0.1` and `::1`).
absl::StatusOr<std::string> ValidateIp(std::string_view ip);

// Reads and validates CLI flags into a `NodeConfig`.
absl::StatusOr<NodeConfig> ReadNodeConfig();

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_FLAGS_H_
