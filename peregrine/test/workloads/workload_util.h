#ifndef PEREGRINE_TEST_WORKLOADS_WORKLOAD_UTIL_H_
#define PEREGRINE_TEST_WORKLOADS_WORKLOAD_UTIL_H_

#include <cstdint>
#include <memory>

#include "absl/log/log.h"
#include "peregrine/test/workloads/kv_cache.h"
#include "peregrine/test/workloads/serial_fixed_write.h"
#include "peregrine/test/workloads/workload_generator.h"

namespace peregrine {

// Factory function to instantiate the requested workload generator.
inline std::unique_ptr<WorkloadGenerator> CreateWorkload(
    WorkloadType workload) {
  switch (workload) {
    case WorkloadType::kSerialFixedWrite:
      return SerialFixedWrite::Create();
    case WorkloadType::kKvCache:
      return KvCache::Create();
  }
  LOG(FATAL) << "Unknown workload: " << static_cast<int>(workload);
}

// Returns the transfer size in bytes for the specified workload.
inline uint64_t GetXferSize(WorkloadType workload) {
  return CreateWorkload(workload)->TotalSizeBytes();
}

}  // namespace peregrine

#endif  // PEREGRINE_TEST_WORKLOADS_WORKLOAD_UTIL_H_
