#ifndef PEREGRINE_SRC_INTERNAL_LIB_RATE_LIMITER_H_
#define PEREGRINE_SRC_INTERNAL_LIB_RATE_LIMITER_H_

#include <algorithm>
#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "peregrine/src/util/strong_int.h"

namespace peregrine::internal {

DEFINE_STRONG_INT_TYPE(BytesPerSec, uint64_t);

// This class implements a blocking rate limiter.
// It is thread-safe.
class BlockingRateLimiter {
 public:
  // Creates a rate limiter with the rate clamped to at least 1 byte/sec.
  explicit BlockingRateLimiter(BytesPerSec rate)
      : rate_(std::max(uint64_t{1}, rate.value())),
        ns_per_byte_(1e9 / rate_.value()),
        deadline_(-absl::InfiniteDuration()) {}

  // Creates a rate limiter with the rate clamped to at least 1 byte/sec.
  explicit BlockingRateLimiter(uint64_t bytes_per_sec)
      : BlockingRateLimiter(BytesPerSec{bytes_per_sec}) {}

  // Returns the rate in bytes per second.
  BytesPerSec Rate() const { return rate_; }

  // Consumes the given number of bytes from the rate limiter.
  // It will block until the given number of bytes can be consumed
  // without exceeding the rate limit.
  void Consume(uint64_t bytes) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Reserves a slot for `bytes` on the shared timeline and returns how long
  // the caller must wait before the slot ends.
  absl::Duration reserve(uint64_t bytes) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  const BytesPerSec rate_;
  const double ns_per_byte_;
  absl::Mutex mu_;
  absl::Duration deadline_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_RATE_LIMITER_H_
