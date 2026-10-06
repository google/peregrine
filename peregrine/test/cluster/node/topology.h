#ifndef PEREGRINE_TEST_CLUSTER_NODE_TOPOLOGY_H_
#define PEREGRINE_TEST_CLUSTER_NODE_TOPOLOGY_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {

// Parses a comma-separated list of "<ip>:<port>@<raddr_uint64>" target strings.
// Returns an empty vector if `raw` is empty or whitespace-only.
absl::StatusOr<std::vector<PeerTarget>> ParseTargets(std::string_view raw);

// Formats a list of `PeerTarget` into comma-separated "<endpoint>@<raddr>".
std::string FormatTargets(absl::Span<const PeerTarget> targets);

// Parses traffic pattern string ("all_to_all" or "round_robin").
absl::StatusOr<TrafficPattern> ParseTrafficPattern(std::string_view raw);

// Returns string representation of `TrafficPattern`.
std::string_view TrafficPatternToString(TrafficPattern pattern);

// Computes the outbound target list for each local instance i in
// [0, num_local_instances).
//
// - If `all_targets` is empty (passive server mode), returns
//   `num_local_instances` empty vectors.
// - `kAllToAll`: assigns every target in `all_targets` to each local instance
//   (excluding `local_endpoints[i]` when `exclude_self` is true).
// - `kRoundRobin`: assigns `all_targets[(node_index * num_local_instances + i)
//   % all_targets.size()]` to local instance `i`.
std::vector<std::vector<PeerTarget>> BuildTargetAssignments(
    int node_index, int num_local_instances,
    absl::Span<const PeerTarget> all_targets, TrafficPattern pattern,
    bool exclude_self = false,
    absl::Span<const std::string> local_endpoints = {});

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_TOPOLOGY_H_
