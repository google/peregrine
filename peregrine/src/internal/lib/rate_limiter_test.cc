#include "peregrine/src/internal/lib/rate_limiter.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/time/time.h"
#include "peregrine/src/util/clock.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

constexpr uint64_t kM = 1'000'000;
constexpr uint64_t kG = 1'000'000'000;

TEST(RateLimiterTest, ZeroBandwidthClampsToOneBytePerSec) {
  BlockingRateLimiter l(0);
  EXPECT_EQ(l.Rate(), BytesPerSec{1});
}

TEST(RateLimiterTest, ZeroBytesDoesNotSleep) {
  BlockingRateLimiter l(1);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(0);
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_LE(elapsed, absl::Milliseconds(20));
}

TEST(RateLimiterTest, SingleByteSleepsBriefly) {
  BlockingRateLimiter l(1 * kM);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(1);  // 1 us
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_LE(elapsed, absl::Milliseconds(20));
}

TEST(RateLimiterTest, HighBandwidthSleepsBriefly) {
  BlockingRateLimiter l(999 * kM);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(1024 * 1024);  // ~1 ms
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_LE(elapsed, absl::Milliseconds(20));
}

TEST(RateLimiterTest, PacesBelowOneGigabytePerSec) {
  BlockingRateLimiter l(100 * kM);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(5 * kM);  // 50 ms
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_GE(elapsed, absl::Milliseconds(30));
  EXPECT_LE(elapsed, absl::Milliseconds(90));
}

TEST(RateLimiterTest, PacesAboveOneGigabytePerSec) {
  BlockingRateLimiter l(800 * kG);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(40 * kG);  // 50 ms
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_GE(elapsed, absl::Milliseconds(30));
  EXPECT_LE(elapsed, absl::Milliseconds(90));
}

TEST(RateLimiterTest, PacesNonIntegralNanosPerByte) {
  BlockingRateLimiter l(600 * kM);
  const absl::Duration start = util::MonotonicNow();
  l.Consume(30 * kM);  // 50 ms
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_GE(elapsed, absl::Milliseconds(30));
  EXPECT_LE(elapsed, absl::Milliseconds(90));
}

TEST(RateLimiterTest, ConcurrentTimelineAdvancement) {
  BlockingRateLimiter l(1'000 * kG);
  const absl::Duration start = util::MonotonicNow();
  util::Thread t1([&] { l.Consume(20 * kG); });  // 20 ms
  util::Thread t2([&] { l.Consume(30 * kG); });  // 30 ms
  t1.join();
  t2.join();
  const absl::Duration elapsed = util::MonotonicNow() - start;
  EXPECT_GE(elapsed, absl::Milliseconds(30));
  EXPECT_LE(elapsed, absl::Milliseconds(90));
}

}  // namespace
}  // namespace peregrine::internal::testing
