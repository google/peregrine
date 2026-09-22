#include "src/internal/request/request_tracker.h"

#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/metrics/engine_metrics.h"

namespace peregrine::internal::testing {
namespace {

class RequestTrackerTest : public ::testing::Test {
 protected:
  RequestTrackerTest() : metrics_(), rt_(metrics_) {
    CHECK(rt_.IsEmpty());
    CHECK_EQ(rt_.Check(kHandle), Status::kNotFound);
  }

 protected:
  EngineMetrics metrics_;
  RequestTracker rt_;
};

TEST_F(RequestTrackerTest, Send) {
  RequestTracker::OnCompleteCallback on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, {kReqId}, absl::Now(), std::move(on_complete)));
  ChunkTracker& send = rt_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    send.Set(index);
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);

  rt_.Remove(kHandle);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, Recv) {
  RequestTracker::OnCompleteCallback on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, {kReqId}, absl::Now(), std::move(on_complete)));
  ChunkTracker& recv = rt_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    ASSERT_EQ(recv.Acquire(index), ChunkTracker::ChunkStatus::kEmpty);
    recv.Release(index, /*success=*/true);
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);

  rt_.Remove(kHandle);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, SetMethod) {
  RequestTracker::OnCompleteCallback on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, {kReqId}, absl::Now(), std::move(on_complete)));
  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);

  rt_.Remove(kHandle);
  EXPECT_TRUE(rt_.IsEmpty());
}

TEST_F(RequestTrackerTest, CompletionCallbackSingleRequest) {
  int callback_count = 0;
  Status completed_status = Status::kNotFound;
  auto on_complete = [&](Status s) {
    ++callback_count;
    completed_status = s;
  };
  ASSERT_TRUE(rt_.Add(kHandle, {kReqId}, absl::Now(), std::move(on_complete)));

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(callback_count, 0);
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }

  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, CompletionCallbackMultipleRequests) {
  int callback_count = 0;
  Status completed_status = Status::kNotFound;
  auto on_complete = [&](Status s) {
    ++callback_count;
    completed_status = s;
  };
  const std::vector<ReqId> reqs = {kReqId, kReqId2};
  ASSERT_TRUE(rt_.Add(kHandle, reqs, absl::Now(), std::move(on_complete)));

  // Complete first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request partially.
  for (int i = 0; i < kNumChunks - 1; ++i) {
    rt_.Update(kHandle, kReqId2, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request fully.
  rt_.Update(kHandle, kReqId2, kNumChunks, chunk_t(kNumChunks - 1));
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, MultipleRequestsPartialProgress) {
  RequestTracker::OnCompleteCallback on_complete = nullptr;
  const std::vector<ReqId> reqs = {kReqId, kReqId2};
  ASSERT_TRUE(rt_.Add(kHandle, reqs, absl::Now(), std::move(on_complete)));

  // Complete the first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete the second request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId2, kNumChunks, chunk_t(i));
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);
}

}  // namespace
}  // namespace peregrine::internal::testing
