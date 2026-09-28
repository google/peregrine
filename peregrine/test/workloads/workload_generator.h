#ifndef PEREGRINE_TEST_WORKLOADS_WORKLOAD_GENERATOR_H_
#define PEREGRINE_TEST_WORKLOADS_WORKLOAD_GENERATOR_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine {

// Traffic workload type.
enum class WorkloadType {
  kSerialFixedWrite,
  kKvCache,
};

// Parses the workload type from its string representation.
inline WorkloadType ParseWorkloadType(std::string_view s) {
  if (absl::EqualsIgnoreCase(s, "serial_fixed_write")) {
    return WorkloadType::kSerialFixedWrite;
  } else if (absl::EqualsIgnoreCase(s, "kv_cache")) {
    return WorkloadType::kKvCache;
  } else {
    LOG(FATAL) << "invalid workload: " << s
               << ". Expected 'serial_fixed_write' or 'kv_cache'.";
  }
}

// Abstract interface for workload generators that map workloads into generic
// Peregrine transfer requests.
class WorkloadGenerator {
 public:
  virtual ~WorkloadGenerator() = default;

  // Name of the workload (e.g. "serial_fixed_write", "kv_cache").
  virtual std::string_view Name() const = 0;

  // Returns the total transfer size in bytes for the workload.
  virtual uint64_t TotalSizeBytes() const = 0;

  // Generates the batch of Peregrine requests given the local and remote
  // base buffer addresses.
  virtual std::vector<peregrine::Request> GenerateRequests(
      peregrine::Byte* base_laddr, peregrine::Byte* base_raddr) const = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_TEST_WORKLOADS_WORKLOAD_GENERATOR_H_
