#include "src/internal/request/request_tracker.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "src/api/transport_types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal::testing {
namespace {

class RequestTrackerTest : public ::testing::Test {
 protected:
  RequestTrackerTest() : tracker_() {
    CHECK(tracker_.IsEmpty());
    CHECK_EQ(tracker_.Check(kHandle), Status::kNotFound);
  }

 protected:
  RequestTracker tracker_;
};

TEST_F(RequestTrackerTest, Send) {
  ASSERT_TRUE(tracker_.Add(kHandle));
  ChunkTracker* send = tracker_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(tracker_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    send->Set(index);
  }
  EXPECT_EQ(tracker_.Check(kHandle), Status::kSuccess);

  tracker_.Remove(kHandle);
  EXPECT_TRUE(tracker_.IsEmpty());
  EXPECT_EQ(tracker_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, Recv) {
  ASSERT_TRUE(tracker_.Add(kHandle));
  ChunkTracker* recv = tracker_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(tracker_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    ASSERT_TRUE(recv->Acquire(index));
    recv->Release(index, /*success=*/true);
  }
  EXPECT_EQ(tracker_.Check(kHandle), Status::kSuccess);

  tracker_.Remove(kHandle);
  EXPECT_TRUE(tracker_.IsEmpty());
  EXPECT_EQ(tracker_.Check(kHandle), Status::kNotFound);
}

}  // namespace
}  // namespace peregrine::internal::testing
