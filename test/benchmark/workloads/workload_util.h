#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_

#include <memory>
#include <string_view>

#include "absl/log/log.h"
#include "src/api/transport.h"
#include "test/benchmark/types.h"
#include "test/benchmark/workloads/serial_fixed_write.h"
#include "test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

// Factory function to instantiate the requested workload.
inline std::unique_ptr<WorkloadGenerator> CreateWorkload(
    WorkloadType workload, Transport* transport, int app_control_fd,
    std::string_view server_endpoint) {
  switch (workload) {
    case WorkloadType::kSerialFixedWrite:
      return SerialFixedWrite::Create(transport, app_control_fd,
                                      server_endpoint);
  }
  LOG(FATAL) << "Unknown workload: " << ToString(workload);
}

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_UTIL_H_
