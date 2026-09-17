#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "src/api/transport.h"
#include "test/benchmark/workloads/workload_generator.h"

ABSL_DECLARE_FLAG(uint32_t, num_layers);
ABSL_DECLARE_FLAG(uint32_t, num_blocks);
ABSL_DECLARE_FLAG(uint64_t, block_size);

namespace peregrine::benchmark {

// Workload generator that emulates KV-cache transfers across multiple layers
// and blocks (--num_layers, --num_blocks, --block_size) posted as batch writes.
class KvCache : public WorkloadGenerator {
 public:
  KvCache(Transport* transport, int app_control_fd,
          std::string_view server_endpoint, uint32_t num_layers,
          uint32_t num_blocks, uint64_t block_size, uint64_t xfer_size,
          uint32_t num_xfers);

  // Computes the total transfer size based on flags
  // (num_layers * num_blocks * block_size).
  static uint64_t XferSize();

  // Creates a KvCache generator after reading and validating CLI flags.
  static std::unique_ptr<KvCache> Create(Transport* transport,
                                         int app_control_fd,
                                         std::string_view server_endpoint);

  absl::Status Run() override;

 private:
  Transport* const transport_;
  const int app_control_fd_;
  const std::string server_endpoint_;
  const uint32_t num_layers_;
  const uint32_t num_blocks_;
  const uint64_t block_size_;
  const uint64_t xfer_size_;
  const uint32_t num_xfers_;
};

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_
