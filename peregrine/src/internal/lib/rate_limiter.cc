#include "peregrine/src/internal/lib/rate_limiter.h"

#include <algorithm>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/util/clock.h"

namespace peregrine::internal {

absl::Duration BlockingRateLimiter::reserve(uint64_t bytes) {
  DCHECK_GE(bytes, 1);
  DCHECK_GE(sec_per_byte_, 0.0);

  const double b = static_cast<double>(bytes);
  const absl::Duration cost = absl::Seconds(b * sec_per_byte_);
  DCHECK_GE(cost, absl::ZeroDuration());

  absl::MutexLock lock(mu_);
  const absl::Duration now = util::MonotonicNow();
  deadline_ = std::max(now, deadline_) + cost;
  return deadline_ - now;
}

void BlockingRateLimiter::Consume(uint64_t bytes) {
  if (bytes >= 1) {
    const absl::Duration wait = reserve(bytes);
    absl::SleepFor(wait);  // nonpositive wait returns immediately
  }
}

}  // namespace peregrine::internal
