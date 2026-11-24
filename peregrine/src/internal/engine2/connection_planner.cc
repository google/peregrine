#include "peregrine/src/internal/engine2/connection_planner.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/nic/nic_inventory.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal::engine2 {
namespace {

struct RailEndpoints {
  int32_t rail_id = kNoRail;
  const std::vector<Endpoint>* endpoints = nullptr;
};

// Collects active IP listeners on `host` paired with their resolved rail IDs.
std::vector<RailEndpoints> ActiveRailEndpoints(const HostInfo& host) {
  std::vector<RailEndpoints> active;
  active.reserve(host.data_plane_listeners.size());
  for (const NicInfo& listener : host.data_plane_listeners) {
    if (listener.type != util::NicType::kIP || listener.endpoints.empty()) {
      continue;
    }
    if (const std::optional<int32_t> rail =
            RailIdForInterface(host, listener.name);
        rail.has_value()) {
      active.push_back(
          RailEndpoints{.rail_id = *rail, .endpoints = &listener.endpoints});
    }
  }
  return active;
}

// Builds a `ConnectionSpec` for `(l, r)` using the first address-family match,
// falling back to an unbound connect to `r.endpoints[0]` if no family matches.
ConnectionSpec MakeSpec(const PeerId& peer, const RailEndpoints& l,
                        const RailEndpoints& r) {
  for (const Endpoint& remote_ep : *r.endpoints) {
    const int family = remote_ep.GetIpAddr().AddressFamily();
    for (const Endpoint& local_ep : *l.endpoints) {
      if (local_ep.GetIpAddr().AddressFamily() == family) {
        return ConnectionSpec{
            .peer = peer,
            .local_rail_id = l.rail_id,
            .remote_rail_id = r.rail_id,
            .local_bind = Endpoint(local_ep.GetIpAddr(), port_t(0)),
            .remote_ep = remote_ep,
        };
      }
    }
  }
  return ConnectionSpec{
      .peer = peer,
      .local_rail_id = l.rail_id,
      .remote_rail_id = r.rail_id,
      .local_bind = Endpoint(),
      .remote_ep = (*r.endpoints)[0],
  };
}

// Picks the first remote endpoint whose address family exists on `local`,
// falling back to `remote_endpoints[0]`.
Endpoint PickCompatibleRemote(const std::vector<RailEndpoints>& local,
                              const std::vector<Endpoint>& remote_endpoints) {
  const auto has_family = [&](const int family) {
    for (const RailEndpoints& l : local) {
      for (const Endpoint& ep : *l.endpoints) {
        if (ep.GetIpAddr().AddressFamily() == family) return true;
      }
    }
    return false;
  };
  for (const Endpoint& remote_ep : remote_endpoints) {
    if (has_family(remote_ep.GetIpAddr().AddressFamily())) return remote_ep;
  }
  return remote_endpoints[0];
}

// Rail-aligned policy: pairs `self` and `peer` listeners by matching `rail_id`
// and assigns each new connection to the least-loaded rail given `existing`.
// Returns empty if either host lacks listeners or no rail IDs overlap.
std::vector<ConnectionSpec> PlanRailAligned(
    const HostInfo& self, const HostInfo& peer, const size_t needed,
    const absl::Span<const ConnectionSpec> existing) {
  const std::vector<RailEndpoints> local = ActiveRailEndpoints(self);
  const std::vector<RailEndpoints> remote = ActiveRailEndpoints(peer);

  // Build one slot per common rail ID with its existing connection count.
  struct RailSlot {
    ConnectionSpec spec;
    size_t count = 0;
  };
  std::vector<RailSlot> slots;
  slots.reserve(local.size());
  for (const RailEndpoints& l : local) {
    const auto r = std::find_if(
        remote.begin(), remote.end(),
        [&](const RailEndpoints& x) { return x.rail_id == l.rail_id; });
    if (r == remote.end()) continue;
    const size_t count = static_cast<size_t>(std::count_if(
        existing.begin(), existing.end(), [&](const ConnectionSpec& s) {
          return s.local_rail_id == l.rail_id && s.remote_rail_id == l.rail_id;
        }));
    slots.push_back(RailSlot{
        .spec = MakeSpec(peer.control_plane_listener, l, *r), .count = count});
  }
  if (slots.empty()) return {};

  // Sort by rail ID for deterministic tie-breaking across equal-count rails.
  std::sort(slots.begin(), slots.end(),
            [](const RailSlot& a, const RailSlot& b) {
              return a.spec.local_rail_id < b.spec.local_rail_id;
            });

  // Greedily assign each new connection to the least-loaded rail.
  std::vector<ConnectionSpec> plan;
  plan.reserve(needed);
  for (size_t i = 0; i < needed; ++i) {
    auto best = std::min_element(
        slots.begin(), slots.end(),
        [](const RailSlot& a, const RailSlot& b) { return a.count < b.count; });
    ++best->count;
    plan.push_back(best->spec);
  }
  return plan;
}

// Default fallback policy when rail-aligned topology is unavailable:
// - Local unrailed: leaves `local_bind` empty (kernel selects the reachable
//   local NIC) and rotates over remote listeners.
// - Local railed: walks `local x remote` pairs (diagonals first) from a
//   rotating start index so retries advance across cross-pairs.
std::vector<ConnectionSpec> PlanDefault(const HostInfo& self,
                                        const HostInfo& peer,
                                        const size_t needed) {
  const std::vector<RailEndpoints> local = ActiveRailEndpoints(self);
  const std::vector<RailEndpoints> remote = ActiveRailEndpoints(peer);
  if (local.empty() || remote.empty()) return {};

  const bool unrailed_local = self.tcp_nics.empty();
  const size_t total =
      unrailed_local ? remote.size() : local.size() * remote.size();
  static std::atomic<size_t> next_start{0};
  const size_t start =
      next_start.fetch_add(1, std::memory_order_relaxed) % total;

  std::vector<ConnectionSpec> plan;
  plan.reserve(needed);
  for (size_t i = 0; i < needed; ++i) {
    const size_t k = (start + i) % total;
    if (unrailed_local) {
      const RailEndpoints& r = remote[k];
      plan.push_back(ConnectionSpec{
          .peer = peer.control_plane_listener,
          .local_rail_id = kNoRail,
          .remote_rail_id = r.rail_id,
          .local_bind = Endpoint(),
          .remote_ep = PickCompatibleRemote(local, *r.endpoints),
      });
    } else {
      // Diagonal-first indexing across all `local.size() * remote.size()`
      // pairs: `d = 0` walks `(0,0), (1,1), ...`, then shifted cross-pairs.
      const size_t d = k / local.size();
      const size_t li = k % local.size();
      const size_t ri = (li + d) % remote.size();
      plan.push_back(
          MakeSpec(peer.control_plane_listener, local[li], remote[ri]));
    }
  }
  return plan;
}

}  // namespace

std::optional<int32_t> RailIdForInterface(const HostInfo& host,
                                          const std::string_view if_name) {
  if (host.tcp_nics.empty()) return kNoRail;
  for (const nic::TcpNic& nic : host.tcp_nics) {
    if (nic.if_name == if_name) {
      if (nic.rail_id >
          static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
      }
      return static_cast<int32_t>(nic.rail_id);
    }
  }
  return std::nullopt;
}

std::vector<int32_t> ActiveRailIds(const HostInfo& host) {
  if (host.tcp_nics.empty()) return {kNoRail};
  const std::vector<RailEndpoints> active = ActiveRailEndpoints(host);
  if (active.empty()) return {kNoRail};
  std::vector<int32_t> ids;
  ids.reserve(active.size());
  for (const RailEndpoints& entry : active) {
    ids.push_back(entry.rail_id);
  }
  return ids;
}

std::vector<ConnectionSpec> PlanConnections(
    const Config& config, const HostInfo& self, const HostInfo& peer,
    const absl::Span<const ConnectionSpec> existing) {
  const int target = config.num_conns_per_peer;
  if (target <= 0 || existing.size() >= static_cast<size_t>(target)) {
    return {};
  }

  const size_t needed = static_cast<size_t>(target) - existing.size();
  if (!self.tcp_nics.empty() && !peer.tcp_nics.empty()) {
    std::vector<ConnectionSpec> aligned =
        PlanRailAligned(self, peer, needed, existing);
    if (!aligned.empty()) return aligned;
  }

  std::vector<ConnectionSpec> fallback = PlanDefault(self, peer, needed);
  if (!fallback.empty()) {
    LOG_EVERY_N_SEC(WARNING, 60)
        << "no rail-aligned topology between " << self.control_plane_listener
        << " and " << peer.control_plane_listener
        << "; falling back to default round-robin policy "
           "(performance may suffer)";
  }
  return fallback;
}

}  // namespace peregrine::internal::engine2
