#include "peregrine/src/internal/engine2/channel_manager.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/connection_planner.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal::engine2 {
namespace {

using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;

constexpr absl::Duration kWaitTimeout = absl::Seconds(10);

HostInfo LoopbackHost(const char* control_endpoint) {
  HostInfo host;
  host.control_plane_listener = Endpoint::Create(control_endpoint);
  return host;
}

// Records what a channel manager reports.
class Recorder {
 public:
  struct Ready {
    PeerId peer;
    int32_t local_rail_id;
    int32_t remote_rail_id;
    std::unique_ptr<Channel> ch;
  };
  struct Failed {
    PeerId peer;
    int32_t local_rail_id;
    int32_t remote_rail_id;
    absl::Status status;
  };

  void Start(ChannelManager& channel_manager) {
    channel_manager.Start(
        [this](const PeerId& peer, int32_t local_rail_id,
               int32_t remote_rail_id, std::unique_ptr<Channel> ch) {
          absl::MutexLock lock(mu_);
          ready_.push_back(
              {peer, local_rail_id, remote_rail_id, std::move(ch)});
        },
        [this](const PeerId& peer, int32_t local_rail_id,
               int32_t remote_rail_id, absl::Status status) {
          absl::MutexLock lock(mu_);
          failed_.push_back(
              {peer, local_rail_id, remote_rail_id, std::move(status)});
        });
  }

  // Waits until at least `n` channels are ready, and returns them.
  std::vector<Ready> WaitForReady(size_t n) {
    absl::MutexLock lock(mu_);
    const auto enough = [&] {
      mu_.AssertHeld();
      return ready_.size() >= n;
    };
    mu_.AwaitWithTimeout(absl::Condition(&enough), kWaitTimeout);
    return std::move(ready_);
  }

  // Waits until at least `n` connects failed, and returns them.
  std::vector<Failed> WaitForFailed(size_t n) {
    absl::MutexLock lock(mu_);
    const auto enough = [&] {
      mu_.AssertHeld();
      return failed_.size() >= n;
    };
    mu_.AwaitWithTimeout(absl::Condition(&enough), kWaitTimeout);
    return std::move(failed_);
  }

 private:
  absl::Mutex mu_;
  std::vector<Ready> ready_ ABSL_GUARDED_BY(mu_);
  std::vector<Failed> failed_ ABSL_GUARDED_BY(mu_);
};

TEST(ChannelManagerTest, CreateRejectsInvalidConfig) {
  HostInfo self = LoopbackHost("127.0.0.1:10000");
  Config config;
  config.num_conns_per_peer = 0;
  EXPECT_EQ(ChannelManager::Create(config, self), nullptr);
}

TEST(ChannelManagerTest, CreateRecordsListeners) {
  HostInfo self = LoopbackHost("127.0.0.1:10000");
  EXPECT_THAT(ChannelManager::Create(Config{}, self), NotNull());
  EXPECT_THAT(self.data_plane_listeners, Not(IsEmpty()));
}

TEST(ChannelManagerTest, BothEndsLearnEachOtherAndRail) {
  // Declared first, so the channel managers reporting to them go first.
  Recorder a_rec;
  Recorder b_rec;
  HostInfo a = LoopbackHost("127.0.0.1:10001");
  HostInfo b = LoopbackHost("127.0.0.1:10002");
  a.tcp_nics = {{.rail_id = 3, .if_name = "lo"}};
  b.tcp_nics = {{.rail_id = 5, .if_name = "lo"}};
  std::unique_ptr<ChannelManager> a_mgr = ChannelManager::Create(Config{}, a);
  std::unique_ptr<ChannelManager> b_mgr = ChannelManager::Create(Config{}, b);
  ASSERT_THAT(a_mgr, NotNull());
  ASSERT_THAT(b_mgr, NotNull());
  a_rec.Start(*a_mgr);
  b_rec.Start(*b_mgr);

  constexpr int kNumChannels = 3;
  Config plan_config;
  plan_config.num_conns_per_peer = kNumChannels;
  for (const ConnectionSpec& spec : PlanConnections(plan_config, a, b)) {
    a_mgr->Connect(spec);
  }

  std::vector<Recorder::Ready> outbound = a_rec.WaitForReady(kNumChannels);
  std::vector<Recorder::Ready> inbound = b_rec.WaitForReady(kNumChannels);
  ASSERT_EQ(outbound.size(), kNumChannels);
  ASSERT_EQ(inbound.size(), kNumChannels);
  for (const Recorder::Ready& r : outbound) {
    EXPECT_EQ(r.peer, b.control_plane_listener);
    EXPECT_EQ(r.ch->GetPeer(), b.control_plane_listener);
    EXPECT_EQ(r.local_rail_id, 3);
    EXPECT_EQ(r.remote_rail_id, 5);
    EXPECT_FALSE(r.ch->IsBlocking());
  }
  for (const Recorder::Ready& r : inbound) {
    EXPECT_EQ(r.peer, a.control_plane_listener);
    EXPECT_EQ(r.ch->GetPeer(), a.control_plane_listener);
    EXPECT_EQ(r.local_rail_id, 5);
    EXPECT_EQ(r.remote_rail_id, 3);
    EXPECT_FALSE(r.ch->IsBlocking());
  }
  EXPECT_THAT(a_rec.WaitForFailed(0), IsEmpty());
}

TEST(ChannelManagerTest, ReadyChannelCarriesData) {
  // Declared first, so the channel managers reporting to them go first.
  Recorder a_rec;
  Recorder b_rec;
  HostInfo a = LoopbackHost("127.0.0.1:10001");
  HostInfo b = LoopbackHost("127.0.0.1:10002");
  std::unique_ptr<ChannelManager> a_mgr = ChannelManager::Create(Config{}, a);
  std::unique_ptr<ChannelManager> b_mgr = ChannelManager::Create(Config{}, b);
  ASSERT_THAT(a_mgr, NotNull());
  ASSERT_THAT(b_mgr, NotNull());
  a_rec.Start(*a_mgr);
  b_rec.Start(*b_mgr);

  const std::vector<ConnectionSpec> specs = PlanConnections(Config{}, a, b);
  ASSERT_THAT(specs, Not(IsEmpty()));
  a_mgr->Connect(specs[0]);
  std::vector<Recorder::Ready> outbound = a_rec.WaitForReady(1);
  std::vector<Recorder::Ready> inbound = b_rec.WaitForReady(1);
  ASSERT_EQ(outbound.size(), 1);
  ASSERT_EQ(inbound.size(), 1);

  // Both directions carry data right after the handshake.
  const auto send_and_receive = [](Channel& from, Channel& to) {
    char out[] = "data";
    IoVecCursor tx({IoVec{out, sizeof(out)}});
    ASSERT_EQ(from.Write(tx), sizeof(out));
    char in[sizeof(out)] = {};
    IoVecCursor rx({IoVec{in, sizeof(in)}});
    const absl::Time deadline = absl::Now() + kWaitTimeout;
    while (rx.RemainingBytes() > 0 && absl::Now() < deadline) {
      ASSERT_GE(to.Read(rx), 0);
    }
    EXPECT_EQ(std::memcmp(in, out, sizeof(out)), 0);
  };
  send_and_receive(*outbound[0].ch, *inbound[0].ch);
  send_and_receive(*inbound[0].ch, *outbound[0].ch);
}

TEST(ChannelManagerTest, ConnectInvalidRemoteEndpointFails) {
  // Declared first, so the channel manager reporting to it goes first.
  Recorder rec;
  HostInfo self = LoopbackHost("127.0.0.1:10000");
  std::unique_ptr<ChannelManager> mgr = ChannelManager::Create(Config{}, self);
  ASSERT_THAT(mgr, NotNull());
  rec.Start(*mgr);

  const HostInfo peer = LoopbackHost("127.0.0.1:20000");
  mgr->Connect(ConnectionSpec{
      .peer = peer.control_plane_listener,
      .local_rail_id = kNoRail,
      .remote_rail_id = 2,
  });

  std::vector<Recorder::Failed> failed = rec.WaitForFailed(1);
  ASSERT_EQ(failed.size(), 1);
  EXPECT_EQ(failed[0].peer, peer.control_plane_listener);
  EXPECT_EQ(failed[0].local_rail_id, kNoRail);
  EXPECT_EQ(failed[0].remote_rail_id, 2);
  EXPECT_THAT(failed[0].status,
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ChannelManagerTest, ConnectToClosedListenerFails) {
  // Declared first, so the channel manager reporting to it goes first.
  Recorder rec;
  HostInfo self = LoopbackHost("127.0.0.1:10001");
  HostInfo gone = LoopbackHost("127.0.0.1:10002");
  std::unique_ptr<ChannelManager> mgr = ChannelManager::Create(Config{}, self);
  ASSERT_THAT(mgr, NotNull());
  // Takes listener ports, then frees them.
  ASSERT_THAT(ChannelManager::Create(Config{}, gone), NotNull());
  rec.Start(*mgr);

  const std::vector<ConnectionSpec> specs =
      PlanConnections(Config{}, self, gone);
  ASSERT_THAT(specs, Not(IsEmpty()));
  mgr->Connect(specs[0]);

  std::vector<Recorder::Failed> failed = rec.WaitForFailed(1);
  ASSERT_EQ(failed.size(), 1);
  EXPECT_EQ(failed[0].peer, gone.control_plane_listener);
  EXPECT_THAT(failed[0].status, StatusIs(absl::StatusCode::kUnavailable));
}

TEST(ChannelManagerTest, ReachingAnotherPeerFails) {
  // Declared first, so the channel managers reporting to them go first.
  Recorder a_rec;
  Recorder b_rec;
  HostInfo a = LoopbackHost("127.0.0.1:10001");
  HostInfo b = LoopbackHost("127.0.0.1:10002");
  std::unique_ptr<ChannelManager> a_mgr = ChannelManager::Create(Config{}, a);
  std::unique_ptr<ChannelManager> b_mgr = ChannelManager::Create(Config{}, b);
  ASSERT_THAT(a_mgr, NotNull());
  ASSERT_THAT(b_mgr, NotNull());
  a_rec.Start(*a_mgr);
  b_rec.Start(*b_mgr);

  // Host info naming another peer, but listing `b`'s listeners.
  HostInfo stale = b;
  stale.control_plane_listener = Endpoint::Create("127.0.0.1:10003");
  const std::vector<ConnectionSpec> specs = PlanConnections(Config{}, a, stale);
  ASSERT_THAT(specs, Not(IsEmpty()));
  a_mgr->Connect(specs[0]);

  std::vector<Recorder::Failed> failed = a_rec.WaitForFailed(1);
  ASSERT_EQ(failed.size(), 1);
  EXPECT_EQ(failed[0].peer, stale.control_plane_listener);
  EXPECT_THAT(failed[0].status,
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ChannelManagerTest, DestroyWithPendingHandshake) {
  // Declared first, so the channel manager reporting to it goes first.
  Recorder rec;
  HostInfo self = LoopbackHost("127.0.0.1:10001");
  std::unique_ptr<ChannelManager> mgr = ChannelManager::Create(Config{}, self);
  ASSERT_THAT(mgr, NotNull());
  rec.Start(*mgr);

  // Connects without sending a hello, so the handshake stays pending.
  std::unique_ptr<TcpSocket> socket = internal::TcpManager::Connect(
      Endpoint(), self.data_plane_listeners[0].endpoints[0], 1000);
  ASSERT_THAT(socket, NotNull());
  absl::SleepFor(absl::Milliseconds(100));
  mgr.reset();
  EXPECT_THAT(rec.WaitForReady(0), IsEmpty());
}

}  // namespace
}  // namespace peregrine::internal::engine2
