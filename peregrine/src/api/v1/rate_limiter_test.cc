#include "peregrine/src/api/v1/rate_limiter.h"

#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace peregrine::v1::testing {
namespace {

TEST(RateLimiterTest, Basic) {
  TestOnlyRateLimiter l(1'000);
  const absl::Time start = absl::Now();
  l.Consume(10);  // 10 ms
  const absl::Duration elapsed = absl::Now() - start;
  EXPECT_LE(elapsed, absl::Milliseconds(50));
}

}  // namespace
}  // namespace peregrine::v1::testing
