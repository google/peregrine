#include "peregrine/src/internal/engine2/peer_state.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/worker.h"

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

TEST(PeerStateTest, EnqueueTriggersSetupOnceAndWakesAssignedWorkers) {
  PeerState state;
  Worker w1(/*rail_id=*/0);
  Worker w2(/*rail_id=*/0);

  PeerState::WorkerList to_wake;
  EXPECT_TRUE(state.Enqueue(MakePost(Handle(1), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());

  // Already setting up: does not re-trigger setup.
  EXPECT_FALSE(state.Enqueue(MakePost(Handle(2), {1}), to_wake));
  EXPECT_THAT(to_wake, IsEmpty());

  // Duplicate worker assignments are deduplicated in `to_wake`.
  state.OnChannelReady(&w2);
  state.OnChannelReady(&w1);
  state.OnChannelReady(&w2);
  EXPECT_TRUE(state.IsReady());

  EXPECT_FALSE(state.Enqueue(MakePost(Handle(3), {1}), to_wake));
  EXPECT_THAT(to_wake, ElementsAre(&w2, &w1));
}

TEST(PeerStateTest, AllConnectsFailedAbortsAndResetsToIdle) {
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

TEST(PeerStateTest, ReadyChannelPreventsAbortOnFailedConnectOrLookup) {
  PeerState state;
  Worker w(/*rail_id=*/0);
  PeerState::WorkerList to_wake;
  state.Enqueue(MakePost(Handle(1), {1}), to_wake);
  state.OnConnectsStarted(2);
  state.OnChannelReady(&w);

  EXPECT_THAT(state.OnConnectFailed(), IsEmpty());
  EXPECT_THAT(state.AbortSetup(), IsEmpty());
  EXPECT_TRUE(state.IsReady());
  EXPECT_TRUE(state.work_queue().TryPull().has_value());
}

}  // namespace
}  // namespace peregrine::internal::engine2
