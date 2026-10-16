#include "peregrine/src/api/v1/histogram.h"

#include <cstddef>

#include "gtest/gtest.h"

namespace peregrine::v1::testing {
namespace {

TEST(HistogramTest, Basic) {
  Histogram<size_t, size_t> h;
  constexpr size_t kSize = 10;
  for (size_t i = 0; i < kSize; ++i) {
    const size_t key = i;
    const size_t count = i + 1;
    h.Add(key, count);
  }

  EXPECT_EQ(h.size(), kSize);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(h.Get(i), i + 1);
  }
  EXPECT_EQ(h.Get(kSize), 0);

  EXPECT_NE(h.begin(), h.end());
}

}  // namespace
}  // namespace peregrine::v1::testing
