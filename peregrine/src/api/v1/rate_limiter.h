#ifndef PEREGRINE_SRC_API_V1_RATE_LIMITER_H_
#define PEREGRINE_SRC_API_V1_RATE_LIMITER_H_

#include <cstdint>

#include "peregrine/src/internal/lib/rate_limiter.h"

namespace peregrine::v1 {

// A rate limiter for testing only.
// It is thread-safe.
class TestOnlyRateLimiter final {
 public:
  // Constructor.
  explicit TestOnlyRateLimiter(uint64_t bytes_per_sec) : l_(bytes_per_sec) {}

  // Returns the rate in bytes per second.
  uint64_t bandwidth_bytes_per_sec() const { return l_.Rate().value(); }

  // Consumes the given number of bytes from the rate limiter.
  // It will block until the given number of bytes can be consumed
  // without exceeding the rate limit.
  void Consume(uint64_t bytes) { l_.Consume(bytes); }

 private:
  internal::BlockingRateLimiter l_;
};

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_RATE_LIMITER_H_
