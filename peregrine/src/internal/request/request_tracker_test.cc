#include "peregrine/src/internal/request/request_tracker.h"

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::UnorderedElementsAre;

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
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
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
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
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
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, chunk_t(i), /*worker_id=*/0);
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
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);

  for (int i = 0; i < kNumChunks; ++i) {
    EXPECT_EQ(callback_count, 0);
    EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
    rt_.Update(kHandle, kReqId, chunk_t(i), /*worker_id=*/0);
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
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  rt_.FindOrCreate(kHandle, kReqId2, kNumChunks);

  // Complete first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, chunk_t(i), /*worker_id=*/0);
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request partially.
  for (int i = 0; i < kNumChunks - 1; ++i) {
    rt_.Update(kHandle, kReqId2, chunk_t(i), /*worker_id=*/0);
  }
  EXPECT_EQ(callback_count, 0);
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete second request fully.
  rt_.Update(kHandle, kReqId2, chunk_t(kNumChunks - 1), /*worker_id=*/0);
  EXPECT_EQ(callback_count, 1);
  EXPECT_EQ(completed_status, Status::kSuccess);
  EXPECT_TRUE(rt_.IsEmpty());
  EXPECT_EQ(rt_.Check(kHandle), Status::kNotFound);
}

TEST_F(RequestTrackerTest, MultipleRequestsPartialProgress) {
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), std::move(on_complete), TraceContext{}));
  rt_.FindOrCreate(kHandle, kReqId, kNumChunks);
  rt_.FindOrCreate(kHandle, kReqId2, kNumChunks);

  // Complete the first request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId, chunk_t(i), /*worker_id=*/0);
  }
  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);

  // Complete the second request.
  for (int i = 0; i < kNumChunks; ++i) {
    rt_.Update(kHandle, kReqId2, chunk_t(i), /*worker_id=*/0);
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

TEST_F(RequestTrackerTest, PollTraceEvents) {
  tracer_.Configure(TraceConfig{.sample_every_n = 1});
  const TraceContext trace_ctx = tracer_.MaybeStartTrace();
  ASSERT_TRUE(trace_ctx.is_traced);
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), /*on_complete=*/nullptr, trace_ctx));
  ChunkTracker& send = rt_.FindOrCreate(kHandle, kReqId, /*num_chunks=*/1);

  EXPECT_EQ(rt_.Check(kHandle), Status::kInProgress);
  send.Set(chunk_t(0));
  EXPECT_EQ(rt_.Check(kHandle), Status::kSuccess);
  rt_.Remove(kHandle);

  const std::vector<TraceEvent> events = tracer_.Buffer().Snapshot();
  ASSERT_EQ(events.size(), 1);
  EXPECT_EQ(events[0].stage, TraceStage::kPollReaped);
  EXPECT_EQ(events[0].handle, kHandle.value());
  EXPECT_GT(events[0].timestamp_us, 0);
}

TEST_F(RequestTrackerTest, CompletionTraceEvents) {
  tracer_.Configure(TraceConfig{.sample_every_n = 1});
  const TraceContext trace_ctx = tracer_.MaybeStartTrace();
  ASSERT_TRUE(trace_ctx.is_traced);

  // 1. Polled handle: records kAllRequestsDone on the last chunk only.
  ASSERT_TRUE(
      rt_.Add(kHandle, absl::Now(), /*on_complete=*/nullptr, trace_ctx));
  rt_.FindOrCreate(kHandle, kReqId, /*num_chunks=*/2);
  rt_.Update(kHandle, kReqId, chunk_t(0), /*worker_id=*/3);
  rt_.Update(kHandle, kReqId, chunk_t(1), /*worker_id=*/3);

  // 2. Callback handle: records kAllRequestsDone and kCallbackDone.
  constexpr Handle kHandle2(0x5678);
  bool callback_invoked = false;
  ASSERT_TRUE(rt_.Add(
      kHandle2, absl::Now(), [&](Status) { callback_invoked = true; },
      trace_ctx));
  rt_.FindOrCreate(kHandle2, kReqId, /*num_chunks=*/1);
  rt_.Update(kHandle2, kReqId, chunk_t(0), /*worker_id=*/4);
  EXPECT_TRUE(callback_invoked);

  const std::vector<TraceEvent> events = tracer_.Buffer().Snapshot();
  ASSERT_EQ(events.size(), 3);
  EXPECT_EQ(events[0].stage, TraceStage::kAllRequestsDone);
  EXPECT_EQ(events[0].handle, kHandle.value());
  EXPECT_EQ(events[0].reqid, kReqId.value());
  EXPECT_EQ(events[0].chunk_idx, 1);
  EXPECT_EQ(events[0].worker_id, 3);

  EXPECT_EQ(events[1].stage, TraceStage::kAllRequestsDone);
  EXPECT_EQ(events[1].handle, kHandle2.value());
  EXPECT_EQ(events[1].worker_id, 4);

  EXPECT_EQ(events[2].stage, TraceStage::kCallbackDone);
  EXPECT_EQ(events[2].handle, kHandle2.value());
  EXPECT_EQ(events[2].worker_id, 4);
}

TEST_F(RequestTrackerTest, RecvPostCallback) {
  constexpr Metadata kReqMeta1 = {1, 2, 3};
  constexpr Metadata kReqMeta2 = {4, 5, 6};
  constexpr Metadata kPostMeta = {7, 8, 9};
  const RequestInfo reqs[] = {
      {.reqid = kReqId,
       .nchunks = kNumChunks,
       .raddr = kBufferBaseAddr,
       .total_len = 2048,
       .op = Op::kWrite,
       .recipe = {.type = RecipeType::kContiguous},
       .metadata = kReqMeta1},
      {.reqid = kReqId2,
       .nchunks = kNumChunks,
       .raddr = kBufferBaseAddr,
       .total_len = 2048,
       .op = Op::kWrite,
       .recipe = {.type = RecipeType::kContiguous},
       .metadata = kReqMeta2},
  };

  struct PostEvent {
    Status status;
    Metadata post_meta;
    std::vector<Metadata> req_metas;
  };
  std::vector<PostEvent> post_events;
  RequestTracker rt(metrics_, tracer_,
                    [&](Status s, const Metadata& post_meta,
                        absl::Span<const Metadata> req_metas) {
                      post_events.push_back(PostEvent{
                          .status = s,
                          .post_meta = post_meta,
                          .req_metas = std::vector<Metadata>(req_metas.begin(),
                                                             req_metas.end()),
                      });
                    });

  ASSERT_TRUE(rt.Add(kHandle, reqs, kPostMeta));

  ChunkTracker& recv1 = rt.Find(kHandle, kReqId);
  for (int i = 0; i < kNumChunks - 1; ++i) {
    const chunk_t index(i);
    ASSERT_EQ(recv1.Acquire(index), ChunkTracker::ChunkStatus::kEmpty)
        << "i=" << i;
    EXPECT_FALSE(recv1.Release(index, /*success=*/true)) << "i=" << i;
  }
  const chunk_t last_index(kNumChunks - 1);
  ASSERT_EQ(recv1.Acquire(last_index), ChunkTracker::ChunkStatus::kEmpty);
  ASSERT_TRUE(recv1.Release(last_index, /*success=*/true));
  rt.OnReqComplete(kHandle);
  EXPECT_TRUE(post_events.empty());

  // Duplicate Add after partial progress must not reset pending_reqs or
  // metadata.
  EXPECT_TRUE(rt.Add(kHandle, reqs, Metadata{9, 9, 9}));

  ChunkTracker& recv2 = rt.Find(kHandle, kReqId2);
  for (int i = 0; i < kNumChunks - 1; ++i) {
    const chunk_t index(i);
    ASSERT_EQ(recv2.Acquire(index), ChunkTracker::ChunkStatus::kEmpty)
        << "i=" << i;
    EXPECT_FALSE(recv2.Release(index, /*success=*/true)) << "i=" << i;
  }
  ASSERT_EQ(recv2.Acquire(last_index), ChunkTracker::ChunkStatus::kEmpty);
  ASSERT_TRUE(recv2.Release(last_index, /*success=*/true));
  rt.OnReqComplete(kHandle);

  ASSERT_EQ(post_events.size(), 1);
  EXPECT_EQ(post_events[0].status, Status::kSuccess);
  EXPECT_EQ(post_events[0].post_meta, kPostMeta);
  EXPECT_THAT(post_events[0].req_metas,
              UnorderedElementsAre(kReqMeta1, kReqMeta2));
  EXPECT_EQ(rt.Check(kHandle), Status::kSuccess);
}
}  // namespace
}  // namespace peregrine::internal::testing
