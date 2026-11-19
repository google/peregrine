#include "peregrine/src/internal/engine2/peer_state.h"

#include <array>
#include <optional>

#include "gtest/gtest.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/engine2/post.h"

namespace peregrine::internal::engine2 {
namespace {

TEST(PeerStateTest, HostInfoIsRequestedOnce) {
  PeerState peer_state;
  EXPECT_TRUE(peer_state.ShouldGetPeerHostInfo());
  EXPECT_FALSE(peer_state.ShouldGetPeerHostInfo());
}

TEST(PeerStateTest, HostInfoIsRequestedAgainAfterAbort) {
  PeerState peer_state;
  ASSERT_TRUE(peer_state.ShouldGetPeerHostInfo());
  peer_state.AbortPosts();
  EXPECT_TRUE(peer_state.ShouldGetPeerHostInfo());
}

TEST(PeerStateTest, EnqueueAndPurgeAcceptPosts) {
  PeerState peer_state;
  std::array<Byte, 64> buf = {};
  peer_state.Enqueue(Post{
      .handle = Handle(1),
      .requests = {{.op = Op::kWrite, .laddr = buf.data(), .len = buf.size()}},
  });
  peer_state.Purge(Handle(1));
}

TEST(PeerStateTest, TryPullOnEmptyQueueReturnsNothing) {
  PeerState peer_state;
  EXPECT_EQ(peer_state.TryPull(), std::nullopt);
}

}  // namespace
}  // namespace peregrine::internal::engine2
