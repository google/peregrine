#include "src/internal/lib/metric_counter.h"

#include <cstdint>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"

namespace peregrine::internal::testing {
namespace {

TEST(MetricCounterTest, Basic) {
  MetricCounter<int32_t> c;
  EXPECT_EQ(c.Value(), 0);

  c.Add(1);
  EXPECT_EQ(c.Value(), 1);

  c.Add(2);
  EXPECT_EQ(c.Value(), 3);

  c.Clear();
  EXPECT_EQ(c.Value(), 0);
}

TEST(MetricCounterTest, ConcurrentUpdates) {
  MetricCounter<uint64_t> c;
  ASSERT_EQ(c.Value(), 0);

  constexpr int kNumThreads = 10;
  constexpr int kInc = 7;
  constexpr int kExpected = kNumThreads * kInc;

  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&c]() {
      for (int j = 0; j < kInc; ++j) {
        c.Add(1);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_NEAR(c.Value(), kExpected, kInc);
}

}  // namespace
}  // namespace peregrine::internal::testing
