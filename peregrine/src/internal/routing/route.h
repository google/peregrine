#ifndef PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_
#define PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_

#include <cstddef>
#include <string_view>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// True when a socket bound to `source_ip` and connected to `peer`
// ("host:port" or "[v6]:port") would egress through the interface that owns
// `source_ip`, per the kernel's route lookup (RTM_GETROUTE with RTA_DST and
// RTA_SRC, honouring policy-routing rules). False when the kernel would route
// the peer via a different interface -- binding `source_ip` would then send
// packets with a source address foreign to the egress link, which cloud
// anti-spoofing drops. Loopback source and loopback peer are trivially true.
// Errors: `peer` unparsable / unresolvable, `source_ip` not an address of any
// local interface, or netlink failure.
//
// Results are memoized for the lifetime of the process keyed by (source_ip,
// peer) exactly as passed; host routing is assumed static for the job. Errors
// are not memoized, so a transient failure is retried on the next call.
absl::StatusOr<bool> SourceIpRoutesToPeer(std::string_view source_ip,
                                          std::string_view peer);

// This class implements memoized route lookup.
// It is thread-safe.
class Route {
 public:
  // Default constructor.
  Route() = default;

  // Disallow copy and move.
  DISALLOW_COPY(Route);
  DISALLOW_MOVE(Route);

  // Destructor.
  ~Route() = default;

  // Returns the size of the route cache.
  size_t Size() const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock _(mu_);
    return routes_.size();
  }

  // Clears the route cache.
  void Clear() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock _(mu_);
    routes_.clear();
  }

  // Computes whether `src` routes to `dst`.
  static bool Compute(const util::IpAddr& src, const util::IpAddr& dst);

  // Returns true iff `src` routes to `dst`. If the result is not in the cache,
  // it is computed and stored in the cache.
  bool FindOrInsert(const util::IpAddr& src, const util::IpAddr& dst)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  using SrcIp = util::IpAddr;
  using DstIp = util::IpAddr;
  using Key = std::pair<SrcIp, DstIp>;

  mutable absl::Mutex mu_;
  absl::flat_hash_map<Key, bool> routes_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_
