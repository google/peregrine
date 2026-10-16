#include "peregrine/src/api/v1/rate_limiter.h"

#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace peregrine::v1::testing {
namespace {

TEST(RateLimiterTest, Basic) {
  TestOnlyRateLimiter l(1'000);
  const absl::Time start = absl::Now();
  l.Consume(1);  // 1 ms
  const absl::Duration elapsed = absl::Now() - start;
  EXPECT_LE(elapsed, absl::Milliseconds(10));
}

}  // namespace
}  // namespace peregrine::v1::testing
