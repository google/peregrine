#include "peregrine/src/internal/engine2/peer_work_queue.h"

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
#include "peregrine/src/internal/engine2/post.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::ElementsAre;

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

TEST(PeerWorkQueueTest, TryPullSlicesPostsInOrder) {
  PeerWorkQueue queue;
  queue.Enqueue(MakePost(Handle(1), {2, 1}));
  queue.Enqueue(MakePost(Handle(2), {1}));

  auto c0 = queue.TryPull();
  ASSERT_TRUE(c0.has_value());
  EXPECT_EQ(c0->post->post.handle, Handle(1));
  EXPECT_EQ(c0->request_index, 0);
  EXPECT_EQ(c0->chunk_index, 0);

  auto c1 = queue.TryPull();
  ASSERT_TRUE(c1.has_value());
  EXPECT_EQ(c1->post->post.handle, Handle(1));
  EXPECT_EQ(c1->request_index, 0);
  EXPECT_EQ(c1->chunk_index, 1);

  auto c2 = queue.TryPull();
  ASSERT_TRUE(c2.has_value());
  EXPECT_EQ(c2->post->post.handle, Handle(1));
  EXPECT_EQ(c2->request_index, 1);
  EXPECT_EQ(c2->chunk_index, 0);

  auto c3 = queue.TryPull();
  ASSERT_TRUE(c3.has_value());
  EXPECT_EQ(c3->post->post.handle, Handle(2));
  EXPECT_EQ(c3->request_index, 0);
  EXPECT_EQ(c3->chunk_index, 0);

  EXPECT_EQ(queue.TryPull(), std::nullopt);
}

TEST(PeerWorkQueueTest, PurgeDropsMatchingHandle) {
  PeerWorkQueue queue;
  queue.Enqueue(MakePost(Handle(1), {1}));
  queue.Enqueue(MakePost(Handle(2), {1}));
  queue.Purge(Handle(1));

  auto c = queue.TryPull();
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->post->post.handle, Handle(2));
  EXPECT_EQ(queue.TryPull(), std::nullopt);
}

TEST(PeerWorkQueueTest, DrainHandlesClearsQueue) {
  PeerWorkQueue queue;
  queue.Enqueue(MakePost(Handle(1), {1}));
  queue.Enqueue(MakePost(Handle(2), {1}));

  EXPECT_THAT(queue.DrainHandles(), ElementsAre(Handle(1), Handle(2)));
  EXPECT_EQ(queue.TryPull(), std::nullopt);
}

}  // namespace
}  // namespace peregrine::internal::engine2
