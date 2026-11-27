#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/connection_planner.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::NotNull;

constexpr absl::Duration kWaitTimeout = absl::Seconds(10);

class TransferManagerTest : public ::testing::Test {
 protected:
  TransferManagerTest()
      : tracer_(TraceConfig{.sample_every_n = 0}),
        outgoing_(metrics_, tracer_),
        incoming_(metrics_, tracer_) {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
    self_.tcp_nics = {{.rail_id = 0, .if_name = "lo"}};
  }

  std::unique_ptr<TransferManager> CreateTransferManager(
      GetPeerHostInfoFn lookup) {
    return TransferManager::Create(
        Config{}, self_, outgoing_, incoming_, metrics_, tracer_,
        std::move(lookup), ChannelManager::Create(Config{}, self_, metrics_));
  }

  void SubmitWrite(TransferManager& tm, const PeerId& peer, Handle handle,
                   RequestTracker::OnComplete on_complete = nullptr,
                   TraceContext trace_ctx = {}) {
    std::vector<RequestInfo> req_infos = {RequestInfo{
        .reqid = ReqId(1),
        .nchunks = 1,
        .total_len = buf_.size(),
        .op = Op::kWrite,
        .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
    }};
    ASSERT_TRUE(outgoing_.Add(handle, /*start_mono_us=*/0,
                              std::move(on_complete), trace_ctx));
    ASSERT_TRUE(outgoing_.AddInitiatorReqs(handle, req_infos));
    tm.Submit(peer,
              Post{
                  .handle = handle,
                  .requests = {{.op = Op::kWrite,
                                .laddr = buf_.data(),
                                .len = buf_.size()}},
                  .trace_ctx = trace_ctx,
              },
              std::move(req_infos));
  }

  HostInfo self_;
  EngineMetrics metrics_;
  EngineMetrics peer_metrics_;
  TraceRecorder tracer_;
  RequestTracker outgoing_;
  RequestTracker incoming_;
  std::array<Byte, 64> buf_ = {};
};

TEST_F(TransferManagerTest, CreateRejectsNullArguments) {
  auto dummy = [](const PeerId&) -> absl::StatusOr<HostInfo> {
    return absl::UnavailableError("unused");
  };
  EXPECT_EQ(TransferManager::Create(Config{}, self_, outgoing_, incoming_,
                                    metrics_, tracer_, dummy,
                                    /*channel_manager=*/nullptr),
            nullptr);
  EXPECT_EQ(TransferManager::Create(
                Config{}, self_, outgoing_, incoming_, metrics_, tracer_,
                /*get_peer_host_info_fn=*/nullptr,
                ChannelManager::Create(Config{}, self_, metrics_)),
            nullptr);
}

TEST_F(TransferManagerTest, SubmitGetsHostInfoOncePerPeer) {
  absl::Mutex peer_mu;
  std::vector<std::unique_ptr<Channel>> peer_channels;
  absl::Notification peer_accepted;
  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20000");
  auto peer_cm = ChannelManager::Create(Config{}, peer_info, peer_metrics_);
  ASSERT_THAT(peer_cm, NotNull());
  peer_cm->Start(
      [&](const PeerId&, int32_t, int32_t, bool, std::unique_ptr<Channel> ch) {
        absl::MutexLock lock(peer_mu);
        peer_channels.push_back(std::move(ch));
        if (!peer_accepted.HasBeenNotified()) peer_accepted.Notify();
      },
      [](const PeerId&, int32_t, int32_t, absl::Status) {});

  absl::Mutex mu;
  std::vector<PeerId> lookups;
  auto tm = CreateTransferManager(
      [&](const PeerId& peer) -> absl::StatusOr<HostInfo> {
        absl::MutexLock lock(mu);
        lookups.push_back(peer);
        return peer_info;
      });
  ASSERT_THAT(tm, NotNull());

  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(1));
  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(2));
  ASSERT_TRUE(peer_accepted.WaitForNotificationWithTimeout(kWaitTimeout));
  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(3));

  tm.reset();
  absl::MutexLock lock(mu);
  EXPECT_THAT(lookups, ElementsAre(peer_info.control_plane_listener));
}

TEST_F(TransferManagerTest, InboundChannelMarksPeerReadyWithoutLookup) {
  absl::Mutex peer_mu;
  std::vector<std::unique_ptr<Channel>> peer_channels;
  absl::Notification peer_connected;
  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20001");
  auto peer_cm = ChannelManager::Create(Config{}, peer_info, peer_metrics_);
  ASSERT_THAT(peer_cm, NotNull());
  peer_cm->Start(
      [&](const PeerId&, int32_t, int32_t, bool, std::unique_ptr<Channel> ch) {
        absl::MutexLock lock(peer_mu);
        peer_channels.push_back(std::move(ch));
        if (!peer_connected.HasBeenNotified()) peer_connected.Notify();
      },
      [](const PeerId&, int32_t, int32_t, absl::Status) {});

  absl::Mutex mu;
  std::vector<PeerId> lookups;
  auto tm = CreateTransferManager(
      [&](const PeerId& peer) -> absl::StatusOr<HostInfo> {
        absl::MutexLock lock(mu);
        lookups.push_back(peer);
        return absl::UnavailableError("should not be called");
      });
  ASSERT_THAT(tm, NotNull());

  // Remote peer connects to us; handshake marks `peer_info` ready locally.
  const std::vector<ConnectionSpec> specs =
      PlanConnections(Config{}, peer_info, self_);
  ASSERT_FALSE(specs.empty());
  peer_cm->Connect(specs[0]);
  ASSERT_TRUE(peer_connected.WaitForNotificationWithTimeout(kWaitTimeout));
  // Wait briefly for our local poll thread to finish `onChannelReady`.
  absl::SleepFor(absl::Milliseconds(20));

  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(1));
  absl::SleepFor(absl::Milliseconds(20));

  tm.reset();
  absl::MutexLock lock(mu);
  EXPECT_THAT(lookups, IsEmpty());
}

TEST_F(TransferManagerTest, LookupFailureFailsQueuedPostsAndRetries) {
  absl::Mutex mu;
  int calls = 0;
  auto tm =
      CreateTransferManager([&](const PeerId&) -> absl::StatusOr<HostInfo> {
        absl::MutexLock lock(mu);
        ++calls;
        return absl::UnavailableError("lookup failed");
      });
  ASSERT_THAT(tm, NotNull());

  const PeerId peer = Endpoint::Create("127.0.0.1:20002");
  absl::Notification failed1;
  Status status1 = Status::kInProgress;
  SubmitWrite(*tm, peer, Handle(1), [&](Status s) {
    status1 = s;
    failed1.Notify();
  });
  ASSERT_TRUE(failed1.WaitForNotificationWithTimeout(kWaitTimeout));
  EXPECT_EQ(status1, Status::kFailure);

  // Next submit retries the lookup.
  absl::Notification failed2;
  SubmitWrite(*tm, peer, Handle(2), [&](Status) { failed2.Notify(); });
  ASSERT_TRUE(failed2.WaitForNotificationWithTimeout(kWaitTimeout));

  absl::MutexLock lock(mu);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(metrics_.write.errors.Value(), 2);
}

TEST_F(TransferManagerTest, ConnectFailureFailsQueuedPostsAndRetries) {
  const PeerId peer = Endpoint::Create("127.0.0.1:20003");
  HostInfo empty_peer;  // no listeners -> connect fails
  empty_peer.control_plane_listener = peer;

  absl::Mutex mu;
  int calls = 0;
  auto tm =
      CreateTransferManager([&](const PeerId&) -> absl::StatusOr<HostInfo> {
        absl::MutexLock lock(mu);
        ++calls;
        return empty_peer;
      });
  ASSERT_THAT(tm, NotNull());

  absl::Notification failed1;
  Status status1 = Status::kInProgress;
  SubmitWrite(*tm, peer, Handle(1), [&](Status s) {
    status1 = s;
    failed1.Notify();
  });
  ASSERT_TRUE(failed1.WaitForNotificationWithTimeout(kWaitTimeout));
  EXPECT_EQ(status1, Status::kFailure);

  absl::Notification failed2;
  SubmitWrite(*tm, peer, Handle(2), [&](Status) { failed2.Notify(); });
  ASSERT_TRUE(failed2.WaitForNotificationWithTimeout(kWaitTimeout));

  absl::MutexLock lock(mu);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(metrics_.write.errors.Value(), 2);
}

TEST_F(TransferManagerTest, DestroyDuringBlockedLookupIsSafe) {
  absl::Notification in_lookup;
  absl::Notification release_lookup;
  auto tm =
      CreateTransferManager([&](const PeerId&) -> absl::StatusOr<HostInfo> {
        in_lookup.Notify();
        release_lookup.WaitForNotification();
        return absl::UnavailableError("cancelled");
      });
  ASSERT_THAT(tm, NotNull());

  SubmitWrite(*tm, Endpoint::Create("127.0.0.1:20004"), Handle(1));
  ASSERT_TRUE(in_lookup.WaitForNotificationWithTimeout(kWaitTimeout));

  // Unblock the lookup right before destroying; destructor joins setup thread.
  release_lookup.Notify();
  tm.reset();
}

TEST_F(TransferManagerTest, SubmitConnectsUsingActiveListenerRails) {
  Config cfg;
  cfg.allowed_devices = {"lo"};

  HostInfo local_info;
  local_info.control_plane_listener = Endpoint::Create("127.0.0.1:10010");
  local_info.tcp_nics = {
      {.rail_id = 0, .if_name = "filtered_out0"},
      {.rail_id = 1, .if_name = "lo"},
  };

  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20010");
  peer_info.tcp_nics = {
      {.rail_id = 0, .if_name = "filtered_out0"},
      {.rail_id = 1, .if_name = "lo"},
  };
  auto peer_cm = ChannelManager::Create(cfg, peer_info, peer_metrics_);
  ASSERT_THAT(peer_cm, NotNull());
  absl::Notification peer_accepted;
  int32_t accepted_local_rail = 0;
  int32_t accepted_remote_rail = 0;
  peer_cm->Start(
      [&](const PeerId&, int32_t local_rail, int32_t remote_rail, bool,
          std::unique_ptr<Channel>) {
        accepted_local_rail = local_rail;
        accepted_remote_rail = remote_rail;
        if (!peer_accepted.HasBeenNotified()) peer_accepted.Notify();
      },
      [](const PeerId&, int32_t, int32_t, absl::Status) {});

  auto tm = TransferManager::Create(
      cfg, local_info, outgoing_, incoming_, metrics_, tracer_,
      [&](const PeerId&) -> absl::StatusOr<HostInfo> { return peer_info; },
      ChannelManager::Create(cfg, local_info, metrics_));
  ASSERT_THAT(tm, NotNull());

  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(1));
  ASSERT_TRUE(peer_accepted.WaitForNotificationWithTimeout(kWaitTimeout));
  EXPECT_EQ(accepted_local_rail, 1);
  EXPECT_EQ(accepted_remote_rail, 1);
}

TEST_F(TransferManagerTest, ColdStartTracedSubmitRecordsConnSetupStages) {
  Config cfg;
  cfg.num_conns_per_peer = 2;
  cfg.allowed_devices = {"lo"};

  absl::Mutex accepted_mu;
  std::vector<std::unique_ptr<Channel>> peer_channels;
  int accepted_count = 0;
  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20011");
  auto peer_cm = ChannelManager::Create(cfg, peer_info, peer_metrics_);
  ASSERT_THAT(peer_cm, NotNull());
  peer_cm->Start(
      [&](const PeerId&, int32_t, int32_t, bool, std::unique_ptr<Channel> ch) {
        absl::MutexLock lock(accepted_mu);
        peer_channels.push_back(std::move(ch));
        ++accepted_count;
      },
      [](const PeerId&, int32_t, int32_t, absl::Status) {});

  auto tm = TransferManager::Create(
      cfg, self_, outgoing_, incoming_, metrics_, tracer_,
      [&](const PeerId&) -> absl::StatusOr<HostInfo> { return peer_info; },
      ChannelManager::Create(cfg, self_, metrics_));
  ASSERT_THAT(tm, NotNull());

  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(42),
              /*on_complete=*/nullptr, TraceContext(/*is_traced=*/true));

  // Wait for both connections to be accepted and for kConnSetupDone locally.
  const absl::Time deadline = absl::Now() + kWaitTimeout;
  std::vector<TraceStage> setup_stages;
  while (absl::Now() < deadline) {
    setup_stages.clear();
    for (const TraceEvent& ev : tracer_.Buffer().Snapshot()) {
      if (ev.handle == 42 && (ev.stage == TraceStage::kConnSetupStart ||
                              ev.stage == TraceStage::kGetPeerHostInfoDone ||
                              ev.stage == TraceStage::kConnSetupDone)) {
        setup_stages.push_back(ev.stage);
      }
    }
    int count = 0;
    {
      absl::MutexLock lock(accepted_mu);
      count = accepted_count;
    }
    if (count == 2 && setup_stages.size() >= 3) break;
    absl::SleepFor(absl::Milliseconds(5));
  }
  EXPECT_THAT(setup_stages, ElementsAre(TraceStage::kConnSetupStart,
                                        TraceStage::kGetPeerHostInfoDone,
                                        TraceStage::kConnSetupDone));
}

TEST_F(TransferManagerTest, TcpConnectFailureRecordsConnectAndWriteErrors) {
  Config cfg;
  cfg.allowed_devices = {"lo"};

  // Obtain a loopback listener address and immediately close it so outbound
  // connects are refused.
  HostInfo closed_peer;
  closed_peer.control_plane_listener = Endpoint::Create("127.0.0.1:20012");
  {
    auto temp_cm = ChannelManager::Create(cfg, closed_peer, peer_metrics_);
    ASSERT_THAT(temp_cm, NotNull());
  }
  ASSERT_FALSE(closed_peer.data_plane_listeners.empty());

  auto tm = TransferManager::Create(
      cfg, self_, outgoing_, incoming_, metrics_, tracer_,
      [&](const PeerId&) -> absl::StatusOr<HostInfo> { return closed_peer; },
      ChannelManager::Create(cfg, self_, metrics_));
  ASSERT_THAT(tm, NotNull());

  absl::Notification failed;
  Status status = Status::kInProgress;
  SubmitWrite(*tm, closed_peer.control_plane_listener, Handle(77),
              [&](Status s) {
                status = s;
                failed.Notify();
              });
  ASSERT_TRUE(failed.WaitForNotificationWithTimeout(kWaitTimeout));
  EXPECT_EQ(status, Status::kFailure);
  EXPECT_EQ(metrics_.tcp_connect_failures.Value(), 1);
  EXPECT_EQ(metrics_.write.errors.Value(), 1);
}

TEST_F(TransferManagerTest, ReconnectsAfterAllChannelsClose) {
  Config cfg;
  cfg.num_conns_per_peer = 1;
  cfg.allowed_devices = {"lo"};

  absl::Mutex peer_mu;
  std::vector<std::unique_ptr<Channel>> peer_channels;
  int accepted_count = 0;
  HostInfo peer_info;
  peer_info.control_plane_listener = Endpoint::Create("127.0.0.1:20013");
  auto peer_cm = ChannelManager::Create(cfg, peer_info, peer_metrics_);
  ASSERT_THAT(peer_cm, NotNull());
  peer_cm->Start(
      [&](const PeerId&, int32_t, int32_t, bool, std::unique_ptr<Channel> ch) {
        absl::MutexLock lock(peer_mu);
        peer_channels.push_back(std::move(ch));
        ++accepted_count;
      },
      [](const PeerId&, int32_t, int32_t, absl::Status) {});

  absl::Mutex lookup_mu;
  int lookup_count = 0;
  auto tm = TransferManager::Create(
      cfg, self_, outgoing_, incoming_, metrics_, tracer_,
      [&](const PeerId&) -> absl::StatusOr<HostInfo> {
        absl::MutexLock lock(lookup_mu);
        ++lookup_count;
        return peer_info;
      },
      ChannelManager::Create(cfg, self_, metrics_));
  ASSERT_THAT(tm, NotNull());

  // First submit establishes the initial channel.
  SubmitWrite(*tm, peer_info.control_plane_listener, Handle(1));
  {
    absl::MutexLock lock(peer_mu);
    const auto has_first = [&]() {
      peer_mu.AssertHeld();
      return accepted_count >= 1;
    };
    ASSERT_TRUE(
        peer_mu.AwaitWithTimeout(absl::Condition(&has_first), kWaitTimeout));
    // Close the peer's channel to trigger channel-close notification locally.
    peer_channels.clear();
  }

  // Once the local worker observes the closed channel and resets peer state to
  // idle, a subsequent submit re-runs peer setup and opens a fresh channel.
  const absl::Time deadline = absl::Now() + kWaitTimeout;
  uint64_t next_handle = 2;
  bool reconnected = false;
  while (!reconnected && absl::Now() < deadline) {
    SubmitWrite(*tm, peer_info.control_plane_listener, Handle(next_handle++));
    absl::MutexLock lock(peer_mu);
    const auto has_second = [&]() {
      peer_mu.AssertHeld();
      return accepted_count >= 2;
    };
    reconnected = peer_mu.AwaitWithTimeout(absl::Condition(&has_second),
                                           absl::Milliseconds(50));
  }
  ASSERT_TRUE(reconnected);

  tm.reset();
  absl::MutexLock lock(lookup_mu);
  EXPECT_EQ(lookup_count, 2);
}

}  // namespace
}  // namespace peregrine::internal::engine2
