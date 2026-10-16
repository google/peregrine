#include "peregrine/src/internal/lib/histogram.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"

namespace peregrine::internal::testing {
namespace {

TEST(HistogramTest, EmptyHistogram) {
  Histogram<int, uint32_t> h;
  EXPECT_EQ(h.Get(42), 0);
}

TEST(HistogramTest, SingleKeyRLE) {
  Histogram<size_t, uint32_t> h;
  for (int i = 0; i < 100; ++i) {
    h.Add(/*key=*/7, /*count=*/1);
  }
  EXPECT_EQ(h.Get(7), 100);
  EXPECT_EQ(h.Get(0), 0);
}

TEST(HistogramTest, AddWithCustomCount) {
  Histogram<std::string, uint64_t> h;
  h.Add("alpha", 10);
  h.Add("alpha", 5);
  h.Add("beta", 3);

  EXPECT_EQ(h.Get("alpha"), 15);
  EXPECT_EQ(h.Get("beta"), 3);
  EXPECT_EQ(h.Get("gamma"), 0);
}

TEST(HistogramTest, MultipleKeysInterleaved) {
  Histogram<int, int> h;
  h.Add(1, 1);
  h.Add(1, 1);
  h.Add(2, 1);
  h.Add(2, 1);
  h.Add(1, 1);
  h.Add(3, 1);
  h.Add(2, 1);
  h.Add(3, 1);

  EXPECT_EQ(h.Get(1), 3);
  EXPECT_EQ(h.Get(2), 3);
  EXPECT_EQ(h.Get(3), 2);
  EXPECT_EQ(h.Get(4), 0);
}

TEST(HistogramTest, ExceedsInlinedElements) {
  Histogram<int, int, 2> h;
  constexpr int kSize = 16;
  for (int i = 0; i < kSize; ++i) {
    h.Add(/*key=*/i, /*count=*/i + 1);
  }

  EXPECT_EQ(h.size(), kSize);
  for (int i = 0; i < kSize; ++i) {
    EXPECT_EQ(h.Get(i), i + 1);
  }
}

TEST(HistogramTest, RangeBasedIteration) {
  Histogram<int, int> h;
  h.Add(10, 1);
  h.Add(20, 2);
  h.Add(30, 3);
  EXPECT_EQ(h.size(), 3);

  int sum_keys = 0;
  int sum_counts = 0;
  for (const auto& [k, c] : h) {
    sum_keys += k;
    sum_counts += c;
  }
  EXPECT_EQ(sum_keys, 60);
  EXPECT_EQ(sum_counts, 6);
}

TEST(HistogramDeathTest, AddZeroCount) {
  Histogram<int, int> h;
  EXPECT_DEBUG_DEATH(h.Add(/*key=*/1, /*count=*/0), "count must not be zero");
}

}  // namespace
}  // namespace peregrine::internal::testing
