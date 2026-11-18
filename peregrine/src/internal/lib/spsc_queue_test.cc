#include "peregrine/src/internal/lib/spsc_queue.h"

#include <atomic>
#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {
namespace {

using ::testing::ElementsAre;

// Move-only, non-default-constructible payload tracking live object counts to
// catch missing destructor calls or double-destroys across wrap-arounds and
// unpopped queue destruction.
class TrackedPayload {
 public:
  explicit TrackedPayload(int value, std::atomic<int>* live_count)
      : value_(value), live_count_(live_count) {
    live_count_->fetch_add(1, std::memory_order_relaxed);
  }

  TrackedPayload(const TrackedPayload&) = delete;
  TrackedPayload& operator=(const TrackedPayload&) = delete;

  TrackedPayload(TrackedPayload&& other) noexcept
      : value_(other.value_), live_count_(other.live_count_) {
    live_count_->fetch_add(1, std::memory_order_relaxed);
    other.value_ = -1;
  }

  TrackedPayload& operator=(TrackedPayload&& other) noexcept {
    if (this != &other) {
      value_ = other.value_;
      live_count_ = other.live_count_;
      other.value_ = -1;
    }
    return *this;
  }

  ~TrackedPayload() {
    if (live_count_ != nullptr) {
      live_count_->fetch_sub(1, std::memory_order_relaxed);
    }
  }

  int value() const { return value_; }

 private:
  int value_;
  std::atomic<int>* live_count_;
};

TEST(BoundedSpscQueueTest, RoundsCapacityUpToPowerOfTwo) {
  BoundedSpscQueue<int> zero_cap(/*min_capacity=*/0);
  EXPECT_EQ(zero_cap.Capacity(), 1);

  BoundedSpscQueue<int> non_pow2(/*min_capacity=*/5);
  EXPECT_EQ(non_pow2.Capacity(), 8);

  BoundedSpscQueue<int> exact_pow2(/*min_capacity=*/16);
  EXPECT_EQ(exact_pow2.Capacity(), 16);
}

TEST(BoundedSpscQueueTest, EnforcesCapacityAndPreservesFifoAcrossWrapAround) {
  BoundedSpscQueue<int> q(/*min_capacity=*/4);
  EXPECT_TRUE(q.IsEmpty());
  EXPECT_FALSE(q.IsFull());
  EXPECT_EQ(q.SizeApprox(), 0);

  int out = -1;
  EXPECT_FALSE(q.Dequeue(out));

  EXPECT_TRUE(q.TryEnqueue(10));
  EXPECT_TRUE(q.TryEnqueue(20));
  EXPECT_TRUE(q.TryEnqueue(30));
  EXPECT_TRUE(q.TryEnqueue(40));
  EXPECT_TRUE(q.IsFull());
  EXPECT_FALSE(q.TryEnqueue(50));
  EXPECT_EQ(q.SizeApprox(), 4);

  ASSERT_TRUE(q.Dequeue(out));
  EXPECT_EQ(out, 10);
  ASSERT_TRUE(q.Dequeue(out));
  EXPECT_EQ(out, 20);
  EXPECT_FALSE(q.IsFull());

  // Wrap around the 4-slot ring buffer.
  EXPECT_TRUE(q.TryEnqueue(50));
  EXPECT_TRUE(q.TryEnqueue(60));
  EXPECT_FALSE(q.TryEnqueue(70));

  std::vector<int> drained;
  while (q.Dequeue(out)) {
    drained.push_back(out);
  }
  EXPECT_THAT(drained, ElementsAre(30, 40, 50, 60));
  EXPECT_TRUE(q.IsEmpty());
  EXPECT_EQ(q.SizeApprox(), 0);
}

TEST(BoundedSpscQueueTest, DestroysMoveOnlyNonDefaultConstructibleElements) {
  std::atomic<int> live_count{0};
  {
    BoundedSpscQueue<TrackedPayload> q(/*min_capacity=*/4);
    EXPECT_TRUE(q.TryEnqueue(TrackedPayload(1, &live_count)));
    EXPECT_TRUE(q.TryEnqueue(TrackedPayload(2, &live_count)));
    EXPECT_TRUE(q.TryEnqueue(TrackedPayload(3, &live_count)));
    EXPECT_EQ(live_count.load(), 3);

    TrackedPayload popped(0, &live_count);
    ASSERT_TRUE(q.Dequeue(popped));
    EXPECT_EQ(popped.value(), 1);
    // 2 remaining inside `q` + 1 in `popped`.
    EXPECT_EQ(live_count.load(), 3);
  }
  // Leaving scope must destroy all unpopped elements cleanly.
  EXPECT_EQ(live_count.load(), 0);
}

TEST(BoundedSpscQueueTest, ConcurrentProducerConsumerMaintainsOrder) {
  constexpr int kNumItems = 100'000;
  BoundedSpscQueue<int> q(/*min_capacity=*/128);

  util::Jthread producer([&q] {
    for (int i = 0; i < kNumItems; ++i) {
      while (!q.TryEnqueue(i)) {
        util::Yield();
      }
    }
  });

  for (int expected = 0; expected < kNumItems; ++expected) {
    int actual = -1;
    while (!q.Dequeue(actual)) {
      util::Yield();
    }
    ASSERT_EQ(actual, expected) << "at index " << expected;
  }
  EXPECT_TRUE(q.IsEmpty());
}

TEST(UnboundedSpscQueueTest, GrowsAcrossSegmentsAndReusesAfterBurst) {
  UnboundedSpscQueue<int> q(/*segment_capacity=*/3);
  EXPECT_EQ(q.SegmentCapacity(), 4);
  EXPECT_TRUE(q.IsEmpty());
  EXPECT_FALSE(q.IsFull());

  // Enqueue a burst spanning 10 segments (40 elements) without dequeuing.
  constexpr int kBurstSize = 40;
  for (int i = 0; i < kBurstSize; ++i) {
    q.Enqueue(i);
  }
  EXPECT_FALSE(q.IsEmpty());
  EXPECT_EQ(q.SizeApprox(), kBurstSize);

  for (int i = 0; i < kBurstSize; ++i) {
    int val = -1;
    ASSERT_TRUE(q.Dequeue(val)) << "at index " << i;
    ASSERT_EQ(val, i) << "at index " << i;
  }
  EXPECT_TRUE(q.IsEmpty());
  EXPECT_EQ(q.SizeApprox(), 0);

  int dummy = 0;
  EXPECT_FALSE(q.Dequeue(dummy));

  // Verify both steady-state wrap-around inside 1 segment and repeated
  // multi-segment bursts reusing the cached spare segment.
  for (int round = 0; round < 20; ++round) {
    const int batch = (round % 2 == 0) ? 3 : 10;
    for (int j = 0; j < batch; ++j) {
      q.Enqueue(round * 100 + j);
    }
    for (int j = 0; j < batch; ++j) {
      int val = -1;
      ASSERT_TRUE(q.Dequeue(val)) << "round " << round << " item " << j;
      ASSERT_EQ(val, round * 100 + j) << "round " << round << " item " << j;
    }
  }
  EXPECT_TRUE(q.IsEmpty());
}

TEST(UnboundedSpscQueueTest, DestroysUnpoppedElementsAcrossMultipleSegments) {
  std::atomic<int> live_count{0};
  {
    SpscQueue<TrackedPayload> q(/*segment_capacity=*/4);
    for (int i = 0; i < 11; ++i) {
      q.Enqueue(TrackedPayload(i, &live_count));
    }
    EXPECT_EQ(live_count.load(), 11);

    // Dequeue across the first segment boundary (`0..4`), leaving `5..10`
    // spanning multiple segments plus a populated `spare_seg_`.
    for (int i = 0; i < 5; ++i) {
      TrackedPayload out(0, &live_count);
      ASSERT_TRUE(q.Dequeue(out)) << "at index " << i;
      ASSERT_EQ(out.value(), i) << "at index " << i;
    }
    EXPECT_EQ(live_count.load(), 6);
  }
  EXPECT_EQ(live_count.load(), 0);
}

TEST(UnboundedSpscQueueTest, ConcurrentProducerConsumerWithSmallSegments) {
  constexpr int kNumItems = 100'000;
  // Tiny segment size (2) forces constant wrap-around, segment overflow, and
  // spare-segment recycling races between producer and consumer under TSan.
  UnboundedSpscQueue<std::unique_ptr<int>> q(/*segment_capacity=*/2);

  util::Jthread producer([&q] {
    for (int i = 0; i < kNumItems; ++i) {
      q.Enqueue(std::make_unique<int>(i));
    }
  });

  for (int expected = 0; expected < kNumItems; ++expected) {
    std::unique_ptr<int> actual;
    while (!q.Dequeue(actual)) {
      util::Yield();
    }
    ASSERT_NE(actual, nullptr) << "at index " << expected;
    ASSERT_EQ(*actual, expected) << "at index " << expected;
  }
  EXPECT_TRUE(q.IsEmpty());
  EXPECT_EQ(q.SizeApprox(), 0);
}

}  // namespace
}  // namespace peregrine::internal
