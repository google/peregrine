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
#include "peregrine/src/internal/base/hostinfo.h"
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
    return std::make_unique<Worker>(id, /*rail_id=*/0, self_, outgoing_,
                                    incoming_, tracer_);
  }

  HostInfo self_;
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

}  // namespace
}  // namespace peregrine::internal::engine2
