#include "peregrine/src/util/clock.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/time/time.h"

namespace peregrine::util::testing {
namespace {

TEST(ClockTest, MonotonicNow) {
  for (int i = 0; i < 1000; ++i) {
    const absl::Duration a = MonotonicNow();
    const absl::Duration b = MonotonicNow();
    EXPECT_LE(a, b);
  }
}

TEST(ClockTest, MonoNowUs) {
  for (int i = 0; i < 1000; ++i) {
    const uint64_t a = MonoNowUs();
    const uint64_t b = MonoNowUs();
    EXPECT_GE(a, 1);
    EXPECT_LE(a, b);
  }
}

}  // namespace
}  // namespace peregrine::util::testing
