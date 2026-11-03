#include "peregrine/src/util/clock.h"

#include <cstdint>
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

uint64_t MonoNowUs() {
  return static_cast<uint64_t>(absl::ToInt64Microseconds(MonotonicNow()));
}

}  // namespace peregrine::util
