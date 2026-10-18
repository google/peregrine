#ifndef PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_
#define PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_

#include <cstddef>
#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

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

  // Returns true iff `src` routes to `dst`.
  // If the result is missing, it is computed and stored in the route cache.
  bool Check(const util::IpAddr& src, const util::IpAddr& dst)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Computes whether `src` routes to `dst`.
  static std::optional<bool> compute(const util::IpAddr& src,
                                     const util::IpAddr& dst);

 private:
  using SrcIp = util::IpAddr;
  using DstIp = util::IpAddr;
  using Key = std::pair<SrcIp, DstIp>;

  mutable absl::Mutex mu_;
  absl::flat_hash_map<Key, bool> routes_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ROUTING_ROUTE_H_
