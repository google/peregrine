#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <array>
#include <memory>

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

namespace peregrine::internal::engine2 {
namespace {

using ::testing::NotNull;

// Peer lookup hook that fails every lookup; no test here connects to a peer.
GetPeerHostInfoFn FailingPeerLookup() {
  return [](const PeerId&,
            absl::AnyInvocable<void(absl::StatusOr<HostInfo>)> on_done) {
    on_done(absl::UnavailableError("no peers in this test"));
  };
}

class TransferManagerTest : public ::testing::Test {
 protected:
  TransferManagerTest() {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
  }

  std::unique_ptr<TransferManager> CreateTransferManager() {
    return TransferManager::Create(Config{}, self_, FailingPeerLookup(),
                                   ChannelManager::Create(Config{}, self_));
  }

  HostInfo self_;
};

TEST_F(TransferManagerTest, Create) {
  EXPECT_THAT(CreateTransferManager(), NotNull());
}

TEST_F(TransferManagerTest, CreateRejectsNullChannelManager) {
  EXPECT_EQ(TransferManager::Create(Config{}, self_, FailingPeerLookup(),
                                    /*channel_manager=*/nullptr),
            nullptr);
}

TEST_F(TransferManagerTest, SubmitAndCancelAcceptPosts) {
  const std::unique_ptr<TransferManager> transfer_manager =
      CreateTransferManager();
  ASSERT_THAT(transfer_manager, NotNull());

  std::array<Byte, 64> buf = {};
  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  transfer_manager->Submit(peer, TransferManager::Post{
                                     .handle = Handle(1),
                                     .requests = {{.op = Op::kWrite,
                                                   .laddr = buf.data(),
                                                   .len = buf.size()}},
                                 });
  transfer_manager->Cancel(peer, Handle(1));
}

}  // namespace
}  // namespace peregrine::internal::engine2
