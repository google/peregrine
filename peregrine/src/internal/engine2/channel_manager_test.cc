#include "peregrine/src/internal/engine2/channel_manager.h"

#include <cstdint>
#include <memory>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"

namespace peregrine::internal::engine2 {
namespace {

using ::absl_testing::StatusIs;
using ::testing::NotNull;

class ChannelManagerTest : public ::testing::Test {
 protected:
  ChannelManagerTest() {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
  }

  HostInfo self_;
};

TEST_F(ChannelManagerTest, CreateRejectsInvalidConfig) {
  Config config;
  config.num_conns_per_peer = 0;
  EXPECT_EQ(ChannelManager::Create(config, self_), nullptr);
}

TEST_F(ChannelManagerTest, Create) {
  EXPECT_THAT(ChannelManager::Create(Config{}, self_), NotNull());
}

TEST_F(ChannelManagerTest, ConnectReportsOutcomeOnce) {
  const std::unique_ptr<ChannelManager> channel_manager =
      ChannelManager::Create(Config{}, self_);
  ASSERT_THAT(channel_manager, NotNull());

  struct Outcome {
    PeerId peer;
    uint32_t local_rail_id;
    uint32_t remote_rail_id;
    absl::Status status;
  };
  int num_outcomes = 0;
  std::optional<Outcome> outcome;
  channel_manager->Start(
      [](std::unique_ptr<Channel>, uint32_t) {
        FAIL() << "no peer connects in this test";
      },
      [&](const PeerId& peer, uint32_t local_rail_id, uint32_t remote_rail_id,
          absl::StatusOr<std::unique_ptr<Channel>> ch) {
        ++num_outcomes;
        outcome = Outcome{peer, local_rail_id, remote_rail_id, ch.status()};
      });

  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20000");
  channel_manager->Connect(peer_info, /*local_rail_id=*/1,
                           /*remote_rail_id=*/2);

  EXPECT_EQ(num_outcomes, 1);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->peer, peer_info.control_plane_listener);
  EXPECT_EQ(outcome->local_rail_id, 1);
  EXPECT_EQ(outcome->remote_rail_id, 2);
  EXPECT_THAT(outcome->status, StatusIs(absl::StatusCode::kUnimplemented));
}

}  // namespace
}  // namespace peregrine::internal::engine2
