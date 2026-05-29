#include "src/internal/chunk/chunk_tracker.h"

#include <cstdint>
#include <memory>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal::testing {
namespace {

TEST(ChunkTrackerTest, OneWriter) {
  ChunkTracker tracker(/*total_num_chunks=*/2);

  // Acquire the chunk #0 and write an ERROR.
  const chunk_t i0(0);
  EXPECT_TRUE(tracker.Acquire(i0));
  EXPECT_TRUE(tracker.IsBusy(i0));
  tracker.Release(i0, /*success=*/false);
  EXPECT_FALSE(tracker.IsBusy(i0));
  EXPECT_FALSE(tracker.IsCompleted());
  EXPECT_TRUE(tracker.IsEmpty());

  // Acquire the chunk #0 again and write a DONE.
  EXPECT_TRUE(tracker.Acquire(i0));
  EXPECT_TRUE(tracker.IsBusy(i0));
  tracker.Release(i0, /*success=*/true);
  EXPECT_FALSE(tracker.IsBusy(i0));
  EXPECT_FALSE(tracker.IsCompleted());
  EXPECT_FALSE(tracker.IsEmpty());

  // Acquire the chunk #1 and write a DONE.
  const chunk_t i1(1);
  EXPECT_TRUE(tracker.Acquire(i1));
  EXPECT_TRUE(tracker.IsBusy(i1));
  tracker.Release(i1, /*success=*/true);
  EXPECT_FALSE(tracker.IsBusy(i1));
  EXPECT_TRUE(tracker.IsCompleted());
  EXPECT_FALSE(tracker.IsEmpty());

  // Acquire won't succeed because all the chunks are DONE.
  EXPECT_FALSE(tracker.Acquire(i0));
  EXPECT_FALSE(tracker.Acquire(i1));
}

class ChunkTrackerStressTest : public ::testing::Test {
 protected:
  ChunkTrackerStressTest()
      : tracker_(std::make_unique<ChunkTracker>(kTotalNumChunks)) {
    CHECK_EQ(tracker_->TotalNumChunks(), kTotalNumChunks);
    CHECK(tracker_->IsEmpty());
    CHECK(!tracker_->IsCompleted());
  }

  chunk_t RandomChunkIndex(absl::BitGen& bitgen) {
    return chunk_t(
        absl::Uniform(absl::IntervalClosedOpen, bitgen, 0U, kTotalNumChunks));
  }

  bool Success(absl::BitGen& bitgen) { return absl::Bernoulli(bitgen, 0.75); }

  void SimulateWork(absl::BitGen& bitgen, chunk_t i) {
    const int n = absl::Uniform(absl::IntervalClosedClosed, bitgen, 5, 10);
    absl::SleepFor(absl::Milliseconds(n));
  }

 protected:
  static constexpr uint32_t kTotalNumChunks = 128;
  std::unique_ptr<ChunkTracker> tracker_;
};

TEST_F(ChunkTrackerStressTest, MultipleWriters) {
  ASSERT_TRUE(tracker_->IsEmpty());

  constexpr int kNumThreads = 32;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int n = 0; n < kNumThreads; ++n) {
    threads.emplace_back([this]() {
      absl::BitGen bitgen;
      while (!tracker_->IsCompleted()) {
        const chunk_t i = RandomChunkIndex(bitgen);
        if (tracker_->Acquire(i)) {
          SimulateWork(bitgen, i);
          if (tracker_->Release(i, Success(bitgen))) break;
        } else {
          absl::SleepFor(absl::Milliseconds(1));
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_TRUE(tracker_->IsCompleted());
  LOG(INFO) << tracker_;
}

}  // namespace
}  // namespace peregrine::internal::testing
