#include "src/internal/buffer/buffer_tracker.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"
#include "src/internal/chunk/tracker.h"

namespace peregrine::internal::testing {
namespace {

class BufferTrackerTest : public ::testing::Test {
 protected:
  BufferTrackerTest() : tracker_() {
    CHECK(tracker_.IsEmpty());
    CHECK(!tracker_.IsDone(kHandle));
  }

 protected:
  BufferTracker tracker_;
};

TEST_F(BufferTrackerTest, Send) {
  ASSERT_TRUE(tracker_.Add(kHandle));
  ASSERT_TRUE(tracker_.Contains(kHandle));
  Tracker* send = tracker_.FindOrCreate(kHandle, kBuffer, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_FALSE(tracker_.IsDone(kHandle));
    const chunk_t index(i);
    send->Set(index);
  }
  EXPECT_TRUE(tracker_.IsDone(kHandle));

  tracker_.Remove(kHandle);
  EXPECT_TRUE(tracker_.IsEmpty());
  EXPECT_FALSE(tracker_.IsDone(kHandle));
}

TEST_F(BufferTrackerTest, Recv) {
  ASSERT_TRUE(tracker_.Add(kHandle));
  ASSERT_TRUE(tracker_.Contains(kHandle));
  Tracker* recv = tracker_.FindOrCreate(kHandle, kBuffer, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_FALSE(tracker_.IsDone(kHandle));
    const chunk_t index(i);
    ASSERT_TRUE(recv->Acquire(index));
    recv->Release(index, /*success=*/true);
  }
  EXPECT_TRUE(tracker_.IsDone(kHandle));

  tracker_.Remove(kHandle);
  EXPECT_TRUE(tracker_.IsEmpty());
  EXPECT_FALSE(tracker_.IsDone(kHandle));
}

}  // namespace
}  // namespace peregrine::internal::testing
