#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/post.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::ElementsAre;
using ::testing::NotNull;

// Peer host info hook that records requests and leaves them pending until the
// test completes them.
class FakeGetPeerHostInfo {
 public:
  GetPeerHostInfoFn Fn() {
    return [this](const PeerId& peer,
                  absl::AnyInvocable<void(absl::StatusOr<HostInfo>)> on_done) {
      peers_.push_back(peer);
      pending_.push_back(std::move(on_done));
    };
  }

  // Completes all pending requests with `result`.
  void CompleteAll(const absl::StatusOr<HostInfo>& result) {
    std::vector<absl::AnyInvocable<void(absl::StatusOr<HostInfo>)>> pending =
        std::move(pending_);
    pending_.clear();
    for (auto& on_done : pending) on_done(result);
  }

  const std::vector<PeerId>& peers() const { return peers_; }

 private:
  std::vector<PeerId> peers_;
  std::vector<absl::AnyInvocable<void(absl::StatusOr<HostInfo>)>> pending_;
};

class TransferManagerTest : public ::testing::Test {
 protected:
  TransferManagerTest() {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
    // Two local rails, so the transfer manager creates two workers.
    self_.tcp_nics = {{.rail_id = 0}, {.rail_id = 1}};
  }

  std::unique_ptr<TransferManager> CreateTransferManager() {
    return TransferManager::Create(Config{}, self_, get_peer_host_info_.Fn(),
                                   ChannelManager::Create(Config{}, self_));
  }

  Post WritePost(Handle handle) {
    return Post{
        .handle = handle,
        .requests = {{.op = Op::kWrite, .laddr = buf_.data(), .len = 64}},
    };
  }

  HostInfo self_;
  FakeGetPeerHostInfo get_peer_host_info_;
  std::array<Byte, 64> buf_ = {};
};

TEST_F(TransferManagerTest, Create) {
  EXPECT_THAT(CreateTransferManager(), NotNull());
}

TEST_F(TransferManagerTest, CreateRejectsNullArguments) {
  EXPECT_EQ(TransferManager::Create(Config{}, self_, get_peer_host_info_.Fn(),
                                    /*channel_manager=*/nullptr),
            nullptr);
  EXPECT_EQ(TransferManager::Create(Config{}, self_,
                                    /*get_peer_host_info_fn=*/nullptr,
                                    ChannelManager::Create(Config{}, self_)),
            nullptr);
}

TEST_F(TransferManagerTest, SubmitGetsHostInfoOncePerPeer) {
  const std::unique_ptr<TransferManager> transfer_manager =
      CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  const Endpoint peer1 = Endpoint::Create("127.0.0.1:20000");
  const Endpoint peer2 = Endpoint::Create("127.0.0.1:30000");
  transfer_manager->Submit(peer1, WritePost(Handle(1)));
  transfer_manager->Submit(peer1, WritePost(Handle(2)));
  transfer_manager->Submit(peer2, WritePost(Handle(3)));
  EXPECT_THAT(get_peer_host_info_.peers(), ElementsAre(peer1, peer2));

  HostInfo peer_info;
  peer_info.control_plane_listener = peer1;
  get_peer_host_info_.CompleteAll(peer_info);
  transfer_manager->Submit(peer1, WritePost(Handle(4)));
  EXPECT_THAT(get_peer_host_info_.peers(), ElementsAre(peer1, peer2));
}

TEST_F(TransferManagerTest, HostInfoFailureRequestsAgainOnNextSubmit) {
  const std::unique_ptr<TransferManager> transfer_manager =
      CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  transfer_manager->Submit(peer, WritePost(Handle(1)));
  get_peer_host_info_.CompleteAll(absl::UnavailableError("host info failed"));
  transfer_manager->Submit(peer, WritePost(Handle(2)));
  EXPECT_THAT(get_peer_host_info_.peers(), ElementsAre(peer, peer));
}

TEST_F(TransferManagerTest, MismatchedHostInfoRequestsAgainOnNextSubmit) {
  const std::unique_ptr<TransferManager> transfer_manager =
      CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  transfer_manager->Submit(peer, WritePost(Handle(1)));
  HostInfo other_info;
  other_info.control_plane_listener = Endpoint::Create("127.0.0.1:30000");
  get_peer_host_info_.CompleteAll(other_info);
  transfer_manager->Submit(peer, WritePost(Handle(2)));
  EXPECT_THAT(get_peer_host_info_.peers(), ElementsAre(peer, peer));
}

TEST_F(TransferManagerTest, HostInfoAfterDestructionIsDropped) {
  std::unique_ptr<TransferManager> transfer_manager = CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  transfer_manager->Submit(peer, WritePost(Handle(1)));
  transfer_manager.reset();
  HostInfo peer_info;
  peer_info.control_plane_listener = peer;
  get_peer_host_info_.CompleteAll(peer_info);  // Must not touch freed memory.
}

TEST_F(TransferManagerTest, CancelAcceptsKnownAndUnknownPeers) {
  const std::unique_ptr<TransferManager> transfer_manager =
      CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  transfer_manager->Submit(peer, WritePost(Handle(1)));
  transfer_manager->Cancel(peer, Handle(1));
  transfer_manager->Cancel(Endpoint::Create("127.0.0.1:30000"), Handle(2));
}

}  // namespace
}  // namespace peregrine::internal::engine2
