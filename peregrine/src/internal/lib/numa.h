#ifndef PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_
#define PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_

#include <vector>

#include "absl/status/status.h"

namespace peregrine::internal {

// Returns the sorted list of logical CPU IDs belonging to `numa_node` that the
// calling thread is allowed to run on. Returns an empty list if `numa_node` is
// negative or unavailable.
std::vector<int> GetNumaCpus(int numa_node);

// Pins the calling thread to logical core `cpu_id`. If `cpu_id < 0`, returns
// `absl::OkStatus()` without changing thread affinity.
absl::Status PinCurrentThreadToCpu(int cpu_id);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_NUMA_H_
