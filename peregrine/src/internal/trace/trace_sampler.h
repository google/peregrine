#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_SAMPLER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_SAMPLER_H_

#include <atomic>
#include <cstdint>

#include "peregrine/src/internal/coding_style.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// Lock-free sampling policy for `Transport::Post()` tracing.
// Thread-safe.
class TraceSampler final {
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  explicit constexpr TraceSampler(uint32_t sample_every_n = 0)
      : sample_every_n_(sample_every_n), counter_(0) {}

  DISALLOW_COPY(TraceSampler);
  DISALLOW_MOVE(TraceSampler);

  ~TraceSampler() = default;

  // Returns the current sampling interval (`0` = disabled, `1` = always-on).
  uint32_t SampleEveryN() const noexcept {
    return sample_every_n_.load(std::memory_order_relaxed);
  }

  // Updates the sampling interval dynamically.
  void SetSampleEveryN(uint32_t sample_every_n) noexcept {
    sample_every_n_.store(sample_every_n, std::memory_order_relaxed);
  }

  // Returns true iff tracing is enabled (`sample_every_n > 0`).
  bool IsEnabled() const noexcept { return SampleEveryN() > 0; }

  // Returns true iff the next `Post()` invocation should be traced.
  bool ShouldSample() noexcept {
    const uint32_t n = sample_every_n_.load(std::memory_order_relaxed);
    if (n == 0) return false;
    if (n == 1) return true;
    const uint64_t idx = counter_.fetch_add(1, std::memory_order_relaxed);
    return (idx % n) == 0;
  }

 private:
  std::atomic<uint32_t> sample_every_n_;
  std::atomic<uint64_t> counter_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_SAMPLER_H_
