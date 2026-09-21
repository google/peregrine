#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "src/api/transport_types.h"
#include "test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

// Workload generator for KV-cache transfers.
class KvCache : public WorkloadGenerator {
 public:
  KvCache(uint32_t num_layers, uint32_t num_blocks, uint64_t block_size);

  // Creates a KvCache generator after reading and validating CLI flags.
  static std::unique_ptr<KvCache> Create();

  std::string_view Name() const override { return "kv_cache"; }

  uint64_t TotalSizeBytes() const override { return xfer_size_; }

  std::vector<peregrine::Request> GenerateRequests(
      peregrine::Byte* base_laddr, peregrine::Byte* base_raddr) const override;

 private:
  const uint32_t num_layers_;
  const uint32_t num_blocks_;
  const uint64_t block_size_;
  const uint64_t xfer_size_;
};

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_KV_CACHE_H_
