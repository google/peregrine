#ifndef PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_

#include <stdint.h>

#include <string_view>

#include "src/api/transport_types.h"

namespace peregrine::benchmark {

// Runs peregrine as receiver.
void RunRcvr(std::string_view ip, uint16_t peregrine_control_port,
             uint16_t app_control_port, int nconns, uint64_t xfer_size,
             TransportType transport_type = TransportType::kTcp);

// Runs peregrine as sender.
void RunSndr(std::string_view ip, uint16_t peregrine_control_port,
             uint16_t app_control_port, int nconns, uint64_t xfer_size,
             std::string_view peer, uint32_t num_xfers,
             TransportType transport_type = TransportType::kTcp);

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
