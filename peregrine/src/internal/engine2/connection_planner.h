#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_CONNECTION_PLANNER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_CONNECTION_PLANNER_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"

namespace peregrine::internal::engine2 {

// Sentinel rail ID used when `HostInfo::tcp_nics` has no topology/rail mapping.
inline constexpr int32_t kNoRail = -1;

// A planned outbound connection with resolved endpoints and rail IDs.
struct ConnectionSpec {
  PeerId peer;
  int32_t local_rail_id = kNoRail;
  int32_t remote_rail_id = kNoRail;
  Endpoint local_bind;  // ephemeral port 0 on matching local IP, or empty
  Endpoint remote_ep;

  bool operator==(const ConnectionSpec&) const = default;
};

// Maps a local interface name on `host` to its rail ID, returning `kNoRail`
// when `host.tcp_nics` is empty, or nullopt if `if_name` is not configured.
std::optional<int32_t> RailIdForInterface(const HostInfo& host,
                                          std::string_view if_name);

// Returns the rail IDs with active IP listeners on `host`, or `{kNoRail}` when
// `host.tcp_nics` is empty or no listeners match.
std::vector<int32_t> ActiveRailIds(const HostInfo& host);

// Returns the NUMA node for the given rail on the host, or the unknown-NUMA
// sentinel if the rail is negative, unknown, or has no known NUMA node.
int NumaNodeForRail(const HostInfo& host, int32_t rail_id);

// Computes the additional outbound connections (with resolved local bind and
// remote listener endpoints) needed to reach `config.num_conns_per_peer`
// between `self` and `peer`. When both hosts advertise overlapping rail
// topology, pairs matching rails and fills least-loaded rails given `existing`;
// otherwise falls back to rotating round-robin across listeners.
std::vector<ConnectionSpec> PlanConnections(
    const Config& config, const HostInfo& self, const HostInfo& peer,
    absl::Span<const ConnectionSpec> existing = {});

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_CONNECTION_PLANNER_H_
