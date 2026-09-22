#include "src/internal/request/request_tracker.h"

#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "src/api/transport_types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/metrics/engine_metrics.h"

namespace peregrine::internal::testing {
namespace {

class RequestTrackerTest : public ::testing::Test {
 protected:
  RequestTrackerTest() : metrics_(), t_(metrics_) {
    CHECK(t_.IsEmpty());
    CHECK_EQ(t_.Check(kHandle), Status::kNotFound);
  }

 protected:
  EngineMetrics metrics_;
  RequestTracker t_;
};

TEST_F(RequestTrackerTest, Send) {
  ASSERT_TRUE(
      t_.Add(kHandle, {kReqId}, absl::Now(), /*on_complete=*/nullptr));
  ChunkTracker& send = t_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    send.Set(index);
  }
  EXPECT_EQ(t_.Check(kHandle), Status::kSuccess);

  t_.Remove(kHandle);
  EXPECT_TRUE(t_.IsEmpty());
  EXPECT_EQ(t_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, Recv) {
  ASSERT_TRUE(
      t_.Add(kHandle, {kReqId}, absl::Now(), /*on_complete=*/nullptr));
  ChunkTracker& recv = t_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    ASSERT_EQ(recv.Acquire(index), ChunkTracker::ChunkStatus::kEmpty);
    recv.Release(index, /*success=*/true);
  }
  EXPECT_EQ(t_.Check(kHandle), Status::kSuccess);

  t_.Remove(kHandle);
  EXPECT_TRUE(t_.IsEmpty());
  EXPECT_EQ(t_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, SetMethod) {
  ASSERT_TRUE(
      t_.Add(kHandle, {kReqId}, absl::Now(), /*on_complete=*/nullptr));
  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);
    t_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(t_.Check(kHandle), Status::kSuccess);

  t_.Remove(kHandle);
  EXPECT_TRUE(t_.IsEmpty());
}

TEST_F(RequestTrackerTest, CompletionCallbackSingleRequest) {
  int callback_count = 0;
  Status completed_status = Status::kNotFound;

  auto callback = [&](Status s) {
    ++callback_count;
    completed_status = s;
  };

  ASSERT_TRUE(t_.Add(kHandle, {kReqId}, absl::Now(), std::move(callback)));

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(callback_count, 0);
    EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);
    t_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }

  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(t_.IsEmpty());
  EXPECT_EQ(t_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, CompletionCallbackMultipleRequests) {
  int callback_count = 0;
  Status completed_status = Status::kNotFound;

  auto callback = [&](Status s) {
    ++callback_count;
    completed_status = s;
  };

  ASSERT_TRUE(
      t_.Add(kHandle, {kReqId, kReqId2}, absl::Now(), std::move(callback)));

  // Complete first request.
  for (int i = 0; i < kNumChunks; ++i) {
    t_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);

  // Complete second request partially.
  for (int i = 0; i < kNumChunks - 1; ++i) {
    t_.Update(kHandle, kReqId2, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);

  // Complete second request fully.
  t_.Update(kHandle, kReqId2, kNumChunks, chunk_t(kNumChunks - 1));
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(t_.IsEmpty());
  EXPECT_EQ(t_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, MultipleRequestsPartialProgress) {
  ASSERT_TRUE(t_.Add(kHandle, {kReqId, kReqId2}, absl::Now(),
                     /*on_complete=*/nullptr));

  // Complete the first request.
  for (int i = 0; i < kNumChunks; ++i) {
    t_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(t_.Check(kHandle), Status::kInProgress);

  // Complete the second request.
  for (int i = 0; i < kNumChunks; ++i) {
    t_.Update(kHandle, kReqId2, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(t_.Check(kHandle), Status::kSuccess);
}

}  // namespace
}  // namespace peregrine::internal::testing
