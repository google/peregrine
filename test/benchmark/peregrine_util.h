#ifndef PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "test/benchmark/types.h"
#include "test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

// Runs peregrine as server.
void RunServer(std::string_view ip, uint16_t peregrine_control_port,
               uint16_t app_control_port, int nconns, uint64_t xfer_size,
               TransportType transport_type = TransportType::kTcp);

// Runs the benchmark loop for the specified workload.
absl::Status RunBenchmark(Transport* transport, int app_control_fd,
                          std::string_view server_endpoint,
                          const WorkloadGenerator& workload,
                          uint32_t num_xfers);

// Runs peregrine as client.
void RunClient(std::string_view ip, uint16_t peregrine_control_port,
               uint16_t app_control_port, int nconns, std::string_view peer,
               TransportType transport_type = TransportType::kTcp,
               WorkloadType workload = WorkloadType::kSerialFixedWrite);

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
