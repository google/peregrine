#include "peregrine/test/benchmark/workloads/kv_cache.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "peregrine/src/api/transport_types.h"

ABSL_FLAG(uint32_t, num_layers, 32,
          "Number of layers for kv_cache workload (default = 32)");

ABSL_FLAG(uint32_t, num_blocks, 64,
          "Number of blocks per layer for kv_cache workload (default = 64)");

ABSL_FLAG(uint64_t, block_size, 1024 * 1024ULL,
          "Block size in bytes for kv_cache workload (default = 1 MiB)");

namespace peregrine::benchmark {

KvCache::KvCache(uint32_t num_layers, uint32_t num_blocks, uint64_t block_size)
    : num_layers_(num_layers),
      num_blocks_(num_blocks),
      block_size_(block_size),
      xfer_size_(static_cast<uint64_t>(num_layers_) * num_blocks_ *
                 block_size_) {
  QCHECK_GT(num_layers_, 0) << "--num_layers must be greater than 0";
  QCHECK_GT(num_blocks_, 0) << "--num_blocks must be greater than 0";
  QCHECK_GT(block_size_, 0) << "--block_size must be greater than 0";
}

std::unique_ptr<KvCache> KvCache::Create() {
  const uint32_t num_layers = absl::GetFlag(FLAGS_num_layers);
  const uint32_t num_blocks = absl::GetFlag(FLAGS_num_blocks);
  const uint64_t block_size = absl::GetFlag(FLAGS_block_size);
  return std::make_unique<KvCache>(num_layers, num_blocks, block_size);
}

std::vector<peregrine::Request> KvCache::GenerateRequests(
    peregrine::Byte* base_laddr, peregrine::Byte* base_raddr) const {
  CHECK(base_laddr != nullptr);
  CHECK(base_raddr != nullptr);

  const size_t total_blocks = static_cast<size_t>(num_layers_) * num_blocks_;
  std::vector<peregrine::Request> requests;
  requests.reserve(total_blocks);

  for (size_t b = 0; b < total_blocks; ++b) {
    const size_t offset = b * static_cast<size_t>(block_size_);
    const size_t len = std::min(static_cast<size_t>(block_size_),
                                static_cast<size_t>(xfer_size_) - offset);
    requests.push_back({
        .op = peregrine::Op::kWrite,
        .laddr = base_laddr + offset,
        .raddr = base_raddr + offset,
        .len = len,
    });
  }
  return requests;
}

}  // namespace peregrine::benchmark
