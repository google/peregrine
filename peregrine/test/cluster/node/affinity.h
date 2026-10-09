#ifndef PEREGRINE_TEST_CLUSTER_NODE_AFFINITY_H_
#define PEREGRINE_TEST_CLUSTER_NODE_AFFINITY_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace peregrine::cluster {

// Parses a Linux CPU list string (e.g., "0-59,120-179" or "0,2,4") into a
// sorted, deduplicated vector of logical CPU IDs.
absl::StatusOr<std::vector<int>> ParseCpuList(std::string_view cpu_list);

// Validates a `--cpu_affinity` specification string ("", "none", "numa:<N>",
// or an explicit CPU list) and returns its normalized form.
absl::StatusOr<std::string> ValidateCpuAffinitySpec(std::string_view spec);

// Resolves a CPU affinity specification into a sorted list of logical CPU IDs:
// - "" or "none" (case-insensitive): returns an empty vector (no-op).
// - "numa:<N>": reads `<sysfs_root>/devices/system/node/node<N>/cpulist`.
// - Otherwise: parses `spec` directly as a CPU list via `ParseCpuList()`.
absl::StatusOr<std::vector<int>> ResolveCpuAffinitySpec(
    std::string_view spec, std::string_view sysfs_root = "/sys");

// Returns the sorted list of logical CPUs currently allowed for the calling
// thread via `sched_getaffinity(2)`.
absl::StatusOr<std::vector<int>> GetCurrentCpuAffinity();

// Resolves `spec` and pins the calling thread/process via
// `sched_setaffinity(2)`. For `numa:<N>` specs, the NUMA node's CPU list is
// intersected with the process's currently allowed cpuset mask so reserved
// system cores are respected.
absl::Status ApplyCpuAffinity(std::string_view spec,
                              std::string_view sysfs_root = "/sys");

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_AFFINITY_H_
