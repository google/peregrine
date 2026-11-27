#include "peregrine/src/internal/engine2/peer_state.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/worker.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

std::shared_ptr<QueuedPost> MakePost(Handle handle,
                                     std::vector<uint32_t> chunks_per_req) {
  auto qp = std::make_shared<QueuedPost>();
  qp->post.handle = handle;
  for (size_t i = 0; i < chunks_per_req.size(); ++i) {
    const uint64_t len = static_cast<uint64_t>(chunks_per_req[i]) * kChunkSize;
    qp->post.requests.push_back(Request{.op = Op::kWrite, .len = len});
    qp->req_infos.push_back(RequestInfo{
        .reqid = ReqId(i + 1),
        .nchunks = chunks_per_req[i],
        .total_len = len,
        .op = Op::kWrite,
        .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
    });
  }
  return qp;
}

class PeerStateTest : public ::testing::Test {
 protected:
  PeerStateTest()
      : tracer_(TraceConfig{.sample_every_n = 0}),
        outgoing_(metrics_, tracer_),
        incoming_(metrics_, tracer_) {}

  std::unique_ptr<Worker> MakeWorker(int id) {
    return std::make_unique<Worker>(id, /*rail_id=*/0, outgoing_, incoming_,
                                    metrics_, tracer_);
  }

  EngineMetrics metrics_;
  TraceRecorder tracer_;
  RequestTracker outgoing_;
  RequestTracker incoming_;
};

TEST_F(PeerStateTest, EnqueueTriggersSetupOnceAndWakesAssignedWorkers) {
  PeerState state;
  auto w1 = MakeWorker(0);
  auto w2 = MakeWorker(1);

  PeerState::WorkerList to_wake;
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());

  // Already setting up: does not re-trigger setup.
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());

  // Duplicate worker assignments are deduplicated in `to_wake`.
  state.OnChannelReady(w2.get());
  state.OnChannelReady(w1.get());
  state.OnChannelReady(w2.get());
  EXPECT_TRUE(state.IsReady());

  EXPECT_FALSE(state.Enqueue(MakePost(Handle(3), {1}), to_wake));
  EXPECT_THAT(to_wake, ElementsAre(w2.get(), w1.get()));
}

TEST_F(PeerStateTest, AllConnectsFailedAbortsAndResetsToIdle) {
  PeerState state;
  PeerState::WorkerList to_wake;
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));

  state.OnConnectsStarted(2);
  EXPECT_THAT(state.OnConnectFailed(), IsEmpty());
  EXPECT_THAT(state.OnConnectFailed(), ElementsAre(Handle(1), Handle(2)));
  EXPECT_EQ(state.work_queue().TryPull(), std::nullopt);

  // Next enqueue triggers setup again.
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(3), {1}), to_wake));
}

TEST_F(PeerStateTest, ReadyChannelPreventsAbortOnFailedConnectOrLookup) {
  PeerState state;
  auto w = MakeWorker(0);
  PeerState::WorkerList to_wake;
  state.Enqueue(MakePost(Handle(1), {1}), to_wake);
  state.OnConnectsStarted(2);
  state.OnChannelReady(w.get());

  EXPECT_THAT(state.OnConnectFailed(), IsEmpty());
  EXPECT_THAT(state.AbortSetup(), IsEmpty());
  EXPECT_TRUE(state.IsReady());
  EXPECT_TRUE(state.work_queue().TryPull().has_value());
}

TEST_F(PeerStateTest, SetupTraceDeliveredOnceAndClearedOnAbort) {
  PeerState state;
  auto w1 = MakeWorker(0);
  auto w2 = MakeWorker(1);
  PeerState::WorkerList to_wake;

  // Aborted setup clears any saved setup trace.
  ASSERT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  EXPECT_TRUE(state.TryBeginSetup(PeerState::SetupTrace{
      .handle = Handle(1), .trace_ctx = TraceContext(/*is_traced=*/true)}));
  EXPECT_THAT(state.AbortSetup(), ElementsAre(Handle(1)));

  // Subsequent traced setup delivers its trace on the first ready channel only.
  ASSERT_TRUE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_TRUE(state.TryBeginSetup(PeerState::SetupTrace{
      .handle = Handle(2), .trace_ctx = TraceContext(/*is_traced=*/true)}));
  const std::optional<PeerState::SetupTrace> first =
      state.OnChannelReady(w1.get());
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->handle, Handle(2));
  EXPECT_TRUE(first->trace_ctx.is_traced);

  EXPECT_EQ(state.OnChannelReady(w2.get()), std::nullopt);
  EXPECT_FALSE(state.TryBeginSetup(PeerState::SetupTrace{
      .handle = Handle(3), .trace_ctx = TraceContext(/*is_traced=*/true)}));
}

TEST_F(PeerStateTest, ChannelCloseTracksPerWorkerCountsAndResetsToIdle) {
  PeerState state;
  auto w1 = MakeWorker(0);
  auto w2 = MakeWorker(1);
  PeerState::WorkerList to_wake;

  ASSERT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  ASSERT_TRUE(state.OnConnectsStarted(2));
  state.OnChannelReady(w1.get(), /*is_outbound=*/true);
  state.OnChannelReady(w2.get(), /*is_outbound=*/true);
  state.OnChannelReady(w2.get(), /*is_outbound=*/false);

  // Closing one of two channels on the second worker keeps both workers awake.
  EXPECT_THAT(state.OnChannelClosed(w2.get()), IsEmpty());
  EXPECT_TRUE(state.IsReady());
  to_wake.clear();
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_THAT(to_wake, ElementsAre(w1.get(), w2.get()));

  // Closing the second worker's last channel removes it from the wake list.
  EXPECT_THAT(state.OnChannelClosed(w2.get()), IsEmpty());
  EXPECT_TRUE(state.IsReady());
  to_wake.clear();
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(3), {1}), to_wake));
  EXPECT_THAT(to_wake, ElementsAre(w1.get()));

  // Closing the last active channel with no pending connects resets to idle
  // and returns all queued handles to fail.
  EXPECT_THAT(state.OnChannelClosed(w1.get()),
              ElementsAre(Handle(1), Handle(2), Handle(3)));
  EXPECT_FALSE(state.IsReady());
  EXPECT_EQ(state.work_queue().TryPull(), std::nullopt);

  // Next enqueue triggers a fresh setup.
  to_wake.clear();
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(4), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());
}

TEST_F(PeerStateTest, ChannelCloseWaitsForPendingConnectsBeforeResetting) {
  PeerState state;
  auto w1 = MakeWorker(0);
  PeerState::WorkerList to_wake;

  ASSERT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  ASSERT_TRUE(state.OnConnectsStarted(2));
  state.OnChannelReady(w1.get(), /*is_outbound=*/true);

  // One outbound connect is still in flight; closing the ready channel returns
  // to setting up without failing queued posts or triggering a second setup.
  EXPECT_THAT(state.OnChannelClosed(w1.get()), IsEmpty());
  EXPECT_FALSE(state.IsReady());
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());
  EXPECT_FALSE(state.TryBeginSetup(PeerState::SetupTrace{}));

  // Once the remaining connect fails, queued posts fail and state resets.
  EXPECT_THAT(state.OnConnectFailed(), ElementsAre(Handle(1), Handle(2)));
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(3), {1}), to_wake));
}

TEST_F(PeerStateTest, ChannelCloseWithPendingConnectRecoversOnNextReady) {
  PeerState state;
  auto w1 = MakeWorker(0);
  auto w2 = MakeWorker(1);
  PeerState::WorkerList to_wake;

  ASSERT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  ASSERT_TRUE(state.OnConnectsStarted(2));
  state.OnChannelReady(w1.get(), /*is_outbound=*/true);
  EXPECT_THAT(state.OnChannelClosed(w1.get()), IsEmpty());
  EXPECT_FALSE(state.IsReady());

  // The second outbound connect completes and drains pending_connects to zero.
  state.OnChannelReady(w2.get(), /*is_outbound=*/true);
  EXPECT_TRUE(state.IsReady());
  to_wake.clear();
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_THAT(to_wake, ElementsAre(w2.get()));

  // Closing the recovered channel now returns to idle immediately.
  EXPECT_THAT(state.OnChannelClosed(w2.get()),
              ElementsAre(Handle(1), Handle(2)));
  EXPECT_FALSE(state.IsReady());
}

}  // namespace
}  // namespace peregrine::internal::engine2
