#include "src/internal/lib/metric_histogram.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"

namespace peregrine::internal::testing {
namespace {

TEST(Log2HistogramTest, DefaultInitialization) {
  Log2Histogram<32> h;
  EXPECT_EQ(h.Sum(), 0);
  EXPECT_EQ(h.NumBuckets(), 32);
  for (size_t i = 0; i < h.NumBuckets(); ++i) {
    EXPECT_EQ(h.Bucket(i), 0);
  }
}

TEST(Log2HistogramTest, RecordBasic) {
  Log2Histogram<32> h;

  h.Record(0);
  EXPECT_EQ(h.Bucket(0), 1);
  EXPECT_EQ(h.Sum(), 0);

  h.Record(1);
  EXPECT_EQ(h.Bucket(1), 1);
  EXPECT_EQ(h.Sum(), 1);

  h.Record(2);
  EXPECT_EQ(h.Bucket(2), 1);
  EXPECT_EQ(h.Sum(), 3);

  h.Record(3);
  EXPECT_EQ(h.Bucket(2), 2);
  EXPECT_EQ(h.Sum(), 6);

  h.Record(4);
  EXPECT_EQ(h.Bucket(3), 1);
  EXPECT_EQ(h.Sum(), 10);

  h.Record(7);
  EXPECT_EQ(h.Bucket(3), 2);
  EXPECT_EQ(h.Sum(), 17);

  h.Record(8);
  EXPECT_EQ(h.Bucket(4), 1);
  EXPECT_EQ(h.Sum(), 25);
  EXPECT_EQ(h.Bucket(31), 0);
}

TEST(Log2HistogramTest, RecordWithCount) {
  Log2Histogram<32> h;

  h.Record(0, 5);
  EXPECT_EQ(h.Bucket(0), 5);
  EXPECT_EQ(h.Sum(), 0);

  h.Record(3, 4);
  EXPECT_EQ(h.Bucket(2), 4);
  EXPECT_EQ(h.Sum(), 12);

  h.Record(8, 2);
  EXPECT_EQ(h.Bucket(4), 2);
  EXPECT_EQ(h.Sum(), 28);
}

TEST(Log2HistogramTest, BoundaryAndOverflow) {
  // 4 buckets:
  // Bucket 0: [0, 1) (0)
  // Bucket 1: [1, 2) (1)
  // Bucket 2: [2, 4) (2, 3)
  // Bucket 3 (Overflow): >= 4
  Log2Histogram<4> h;
  EXPECT_EQ(h.NumBuckets(), 4);

  h.Record(0);
  EXPECT_EQ(h.Bucket(0), 1);

  h.Record(7);
  EXPECT_EQ(h.Bucket(3), 1);

  // 8 is >= 4, should also go to bucket 3.
  h.Record(8);
  EXPECT_EQ(h.Bucket(3), 2);

  h.Record(100, 3);
  EXPECT_EQ(h.Bucket(3), 5);
  EXPECT_EQ(h.Sum(), 0 + 7 + 8 + 300);
}

TEST(Log2HistogramTest, RacyExport) {
  Log2Histogram<4> h;
  h.Record(0, 2);
  h.Record(1, 3);
  h.Record(3, 5);
  h.Record(6, 7);
  h.Record(10, 11);  // overflow (bucket 3)

  std::array<uint64_t, 4> exported = h.RacyExport();
  EXPECT_EQ(exported[0], 2);
  EXPECT_EQ(exported[1], 3);
  EXPECT_EQ(exported[2], 5);
  EXPECT_EQ(exported[3], 18);
  EXPECT_EQ(h.Bucket(3), 18);
}

TEST(Log2HistogramTest, Clear) {
  Log2Histogram<16> h;
  h.Record(0);
  h.Record(5);
  h.Record(100000);  // Overflow for 16 buckets (>= 16384, bucket 15)

  EXPECT_GT(h.Sum(), 0);
  EXPECT_GT(h.Bucket(15), 0);

  h.Clear();
  EXPECT_EQ(h.Sum(), 0);
  EXPECT_EQ(h.Bucket(15), 0);
  for (size_t i = 0; i < h.NumBuckets(); ++i) {
    EXPECT_EQ(h.Bucket(i), 0);
  }
  for (uint64_t val : h.RacyExport()) {
    EXPECT_EQ(val, 0);
  }
}

TEST(Log2HistogramTest, ConcurrentUpdates) {
  Log2Histogram<16> h;

  constexpr int kNumThreads = 10;
  constexpr int kRounds = 100;

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&h]() {
      for (int j = 0; j < kRounds; ++j) {
        h.Record(0);
        h.Record(5, 2);
        h.Record(100000, 3);  // Overflow for 16 buckets (bucket 15)
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(h.Bucket(0), kNumThreads * kRounds);
  // 5 is in [4, 8) -> bucket 3
  EXPECT_EQ(h.Bucket(3), 2 * kNumThreads * kRounds);
  EXPECT_EQ(h.Bucket(15), 3 * kNumThreads * kRounds);
  EXPECT_EQ(h.Sum(),
            static_cast<uint64_t>(0 + 5 * 2 + 100000 * 3) * kNumThreads *
                kRounds);
}

TEST(Log2HistogramTest, OutOfBoundsDeath) {
  Log2Histogram<4> h;
  EXPECT_DEBUG_DEATH(h.Bucket(4), "");
}

}  // namespace
}  // namespace peregrine::internal::testing
