#ifndef PEREGRINE_SRC_UTIL_CLOCK_H_
#define PEREGRINE_SRC_UTIL_CLOCK_H_

#include <cstdint>

#include "absl/time/time.h"

namespace peregrine::util {

// Returns the current time of the monotonic clock, as the duration since an
// unspecified starting point. Unlike `absl::Now()`, it is not affected by wall
// clock jumps, so it is suitable for measuring timeouts.
absl::Duration MonotonicNow();

// Returns the current time of the monotonic clock in microseconds. The value
// is always positive, so 0 can be used as an invalid timestamp.
uint64_t MonoNowUs();

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_CLOCK_H_
