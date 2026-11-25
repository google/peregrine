#ifndef PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_
#define PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace peregrine::internal {

// Sentinel indicating no NUMA node is known or associated (e.g., loopback or
// unrailed hosts).
inline constexpr int kUnknownNumaNode = -1;

// Sentinel indicating no CPU core is assigned (leave the thread unpinned).
inline constexpr int kUnpinnedCpu = -1;

// Returns the sorted list of logical CPU IDs on the given NUMA node that the
// calling thread is allowed to run on. Returns an empty list if the node is
// negative or unavailable.
std::vector<int> GetNumaCpus(int numa_node);

// Pins the calling thread to the given logical CPU core. Negative values leave
// thread affinity unchanged and succeed.
absl::Status PinCurrentThreadToCpu(int cpu_id);

// Assigns distinct allowed CPUs round-robin per NUMA node, caching each node's
// CPU list on first lookup. Not thread-safe.
class NumaCpuAssigner final {
 public:
  using CpuLookupFn = absl::AnyInvocable<std::vector<int>(int) const>;

  NumaCpuAssigner() : lookup_(GetNumaCpus) {}
  explicit NumaCpuAssigner(CpuLookupFn lookup)
      : lookup_(lookup ? std::move(lookup) : CpuLookupFn(GetNumaCpus)) {}

  // Returns the next logical CPU ID on the given NUMA node, wrapping around
  // once all allowed CPUs on that node have been assigned. Returns the unpinned
  // sentinel if the node is negative or has no allowed CPUs.
  int NextCpu(int numa_node) {
    if (numa_node < 0) return kUnpinnedCpu;
    auto [it, inserted] = pools_.try_emplace(numa_node);
    if (inserted) it->second.cpus = lookup_(numa_node);
    if (it->second.cpus.empty()) return kUnpinnedCpu;
    return it->second.cpus[it->second.next++ % it->second.cpus.size()];
  }

 private:
  struct Pool {
    std::vector<int> cpus;
    size_t next = 0;
  };

  CpuLookupFn lookup_;
  absl::flat_hash_map<int, Pool> pools_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_
