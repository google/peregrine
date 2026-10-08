#ifndef PEREGRINE_SRC_UTIL_CLOCK_H_
#define PEREGRINE_SRC_UTIL_CLOCK_H_

#include "absl/time/time.h"

namespace peregrine::util {

// Returns the current time of the monotonic clock, as the duration since an
// unspecified starting point. Unlike `absl::Now()`, it is not affected by wall
// clock jumps, so it is suitable for measuring timeouts.
absl::Duration MonotonicNow();

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_CLOCK_H_
