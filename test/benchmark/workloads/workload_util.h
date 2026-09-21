#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_

#include <cstdint>
#include <memory>

#include "absl/log/log.h"
#include "test/benchmark/types.h"
#include "test/benchmark/workloads/serial_fixed_write.h"
#include "test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

// Factory function to instantiate the requested workload generator.
inline std::unique_ptr<WorkloadGenerator> CreateWorkload(
    WorkloadType workload) {
  switch (workload) {
    case WorkloadType::kSerialFixedWrite:
      return SerialFixedWrite::Create();
  }
  LOG(FATAL) << "Unknown workload: " << ToString(workload);
}

// Returns the transfer size in bytes for the specified workload.
inline uint64_t GetXferSize(WorkloadType workload) {
  return CreateWorkload(workload)->TotalSizeBytes();
}

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_
