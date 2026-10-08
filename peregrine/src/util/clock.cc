#include "peregrine/src/util/clock.h"

#include <ctime>

#include "absl/log/check.h"
#include "absl/time/time.h"

namespace peregrine::util {

absl::Duration MonotonicNow() {
  struct timespec ts;
  const int ret = ::clock_gettime(CLOCK_MONOTONIC, &ts);
  DCHECK_EQ(ret, 0);
  return absl::DurationFromTimespec(ts);
}

}  // namespace peregrine::util
