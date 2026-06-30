#ifndef PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_

#include <stdint.h>

#include <string_view>

namespace peregrine::benchmark {

// Runs peregrine as receiver.
void RunRcvr(bool ipv4, uint16_t port, int nconns, uint64_t xfer_size);

// Runs peregrine as sender.
void RunSndr(bool ipv4, uint16_t port, int nconns, uint64_t xfer_size,
             std::string_view peer, void* raddr, uint32_t num_xfers);

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_PEREGRINE_UTIL_H_
