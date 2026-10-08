#include "peregrine/src/util/clock.h"

#include "gtest/gtest.h"
#include "absl/time/time.h"

namespace peregrine::util::testing {
namespace {

TEST(ClockTest, MonotonicNow) {
  for (int i = 0; i < 1000; ++i) {
    const absl::Duration a = util::MonotonicNow();
    const absl::Duration b = util::MonotonicNow();
    EXPECT_LE(a, b);
  }
}

}  // namespace
}  // namespace peregrine::util::testing
