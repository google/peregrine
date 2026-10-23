#include "peregrine/src/internal/request/request_tracker.h"

#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "peregrine/src/api/trace/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::testing {
namespace {

class RequestTrackerTest : public ::testing::Test {
 protected:
  RequestTrackerTest()
      : metrics_(),
        tracer_(TraceConfig{.sample_every_n = 0}),
        rt_(metrics_, tracer_) {
    CHECK(rt_.IsEmpty());
    CHECK_EQ(rt_.Check(kHandle), Status::kNotFound);
  }

 protected:
  EngineMetrics metrics_;
  TraceRecorder tracer_;
  RequestTracker rt_;
};

TEST_F(RequestTrackerTest, Send) {
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
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
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
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
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, chunk_t(i));
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
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(callback_count, 0);
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, chunk_t(i));
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
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  rt_.FindOrCreate(kHandle, kReqId2, kNumChunks);

  // Complete first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request partially.
  for (int i = 0; i < kNumChunks - 1; ++i) {
    rt_.Update(kHandle, kReqId2, chunk_t(i));
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request fully.
  rt_.Update(kHandle, kReqId2, chunk_t(kNumChunks - 1));
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, MultipleRequestsPartialProgress) {
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(rt_.Add(kHandle, absl::Now(), std::move(on_complete)));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  rt_.FindOrCreate(kHandle, kReqId2, kNumChunks);

  // Complete the first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, chunk_t(i));
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete the second request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId2, chunk_t(i));
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);
}

TEST_F(RequestTrackerTest, RecvRequestInfo) {
  const RequestInfo reqs[] = {
      {.reqid = kReqId,
       .nchunks = kNumChunks,
       .raddr = kBufferBaseAddr,
       .total_len = 2048,
       .op = Op::kWrite,
       .recipe = {.type = RecipeType::kContiguous}},
  };

  ASSERT_TRUE(rt_.Add(kHandle, reqs));
  EXPECT_TRUE(rt_.Add(kHandle, reqs));  // Duplicate POST_INFO is idempotent.

  ChunkTracker& recv = rt_.Find(kHandle, kReqId);
  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    const chunk_t index(i);
    ASSERT_EQ(recv.Acquire(index), ChunkTracker::ChunkStatus::kEmpty);
    recv.Release(index, /*success=*/true);
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);
}

}  // namespace
}  // namespace peregrine::internal::testing
