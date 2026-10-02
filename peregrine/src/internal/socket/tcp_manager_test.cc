#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/lib/metric_counter.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_test_util.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

class TcpManagerTest : public TestWithParam<SocketTestParam> {
 protected:
  TcpManagerTest()
      : cfg_(GetParam()),
        self_(TestOnly_LocalHostInfo(cfg_.family, /*tcp=*/true)),
        mgr_(TcpManager::Create(self_)),
        peers_(self_.data_plane_listeners),
        num_peer_endpoints_(NumPeerEndpoints()) {
    CHECK(self_.IsValid());
    CHECK_NE(mgr_, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }
  static void LongSleep() { absl::SleepFor(absl::Seconds(1)); }

  size_t NumPeerEndpoints() {
    return std::accumulate(peers_.begin(), peers_.end(), size_t{0},
                           [](size_t sum, const NicInfo& ni) {
                             return sum + ni.endpoints.size();
                           });
  }

  size_t ConnectAll(int rounds) {
    size_t count = 0;
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        for (int i = 0; i < rounds; ++i) {
          const int ret = mgr_->Connect(/*self=*/{}, peer, cfg_.blocking);
          CHECK_GE(ret, 0) << "failed to connect to " << peer;
          ++count;
        }
      }
    }
    return count;
  }

  void ConnectAll(MetricCounter<int>& rounds_done, int rounds) {
    for (int i = 0; i < rounds; ++i) {
      for (const NicInfo& ni : peers_) {
        for (const Endpoint& peer : ni.endpoints)
          (void)mgr_->Connect(/*self=*/{}, peer, cfg_.blocking);  // may fail
      }
      rounds_done.Add(1);
    }
  }

  static void Migrate(std::vector<std::unique_ptr<TcpSocket>> from,
                      std::vector<std::unique_ptr<TcpSocket>>& to) {
    for (auto& socket : from) to.push_back(std::move(socket));
  }

  size_t DrainConnected() {
    auto connected = mgr_->GetOutgoingSockets();
    return connected.size();
  }

  size_t DrainConnected(const Endpoint& peer) {
    auto connected = mgr_->GetOutgoingSockets(peer);
    return connected.size();
  }

  void VerifyAllConnected() {
    const absl::Time deadline = absl::Now() + absl::Seconds(5);
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        size_t count = 0;
        while (count == 0 && absl::Now() < deadline) {
          count += DrainConnected(peer);
          absl::SleepFor(absl::Milliseconds(1));
        }
        EXPECT_EQ(count, 1);
        EXPECT_EQ(DrainConnected(peer), 0);
      }
    }
    EXPECT_EQ(DrainConnected(), 0);
  }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::unique_ptr<TcpManager> mgr_;
  const std::vector<NicInfo> peers_;
  const size_t num_peer_endpoints_;
};

INSTANTIATE_TEST_SUITE_P(, TcpManagerTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(TcpManagerTest, StartThenStop) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, StopThenStart) {
  mgr_->Stop();
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ta.join();
}

TEST_P(TcpManagerTest, AcceptBeforeConnect) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  util::Thread tc([&]() { ConnectAll(/*rounds=*/1); });
  tc.join();
  VerifyAllConnected();

  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, ConnectBeforeAccept) {
  util::Thread tc([&]() { ConnectAll(/*rounds=*/1); });
  ShortSleep();

  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  tc.join();
  VerifyAllConnected();

  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, ConcurrentConnects) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  constexpr size_t kNumThreads = 100;
  constexpr int kRoundsPerThread = 10;
  constexpr int kRounds = kNumThreads * kRoundsPerThread;

  std::vector<util::Thread> threads;
  threads.reserve(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      LOG(INFO) << "connecting thread #" << i << "...";
      ConnectAll(kRoundsPerThread);
    });
  }
  LongSleep();  // Give threads some time to connect.

  // Drain sockets concurrently with the connecting threads, keeping them open
  // until the end so that closing one side cannot make the other side hang up
  // before it is produced. Each connect produces one accepted and one
  // connected socket.
  const size_t expected = 2 * kRounds * num_peer_endpoints_;
  const absl::Time deadline = absl::Now() + absl::Seconds(60);
  std::vector<std::unique_ptr<TcpSocket>> all_sockets;
  all_sockets.reserve(expected);
  while (all_sockets.size() < expected && absl::Now() < deadline) {
    Migrate(mgr_->GetIncomingSockets(), all_sockets);
    Migrate(mgr_->GetOutgoingSockets(), all_sockets);
    absl::SleepFor(absl::Milliseconds(1));
  }
  const size_t count = all_sockets.size();
  all_sockets.clear();

  // Potential listen backlog overflows under high concurrency justifies
  // a non-100% threshold even on loopback.
  LOG(INFO) << "accepted/connected=" << count << ", expected=" << expected;
  EXPECT_GE(count, expected * 99 / 100);

  for (auto& tc : threads) tc.join();
  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, StopWhileConnecting) {
  const size_t sockets_before = CountOpenSocketFds();
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  // Hammer Connect() from many threads and call Stop() in the middle of it, so
  // that connects are in flight before, during and after Stop(). Every socket
  // they create must end up either handed out by Get*Sockets() or closed.
  constexpr int kNumThreads = 10;
  constexpr int kRoundsPerThread = 100;
  MetricCounter<int> rounds_done;
  std::vector<util::Thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      LOG(INFO) << "connecting thread #" << i << "...";
      ConnectAll(rounds_done, kRoundsPerThread);
    });
  }

  // Drain (and close) until a quarter of the rounds are done, then stop while
  // the rest are still connecting.
  constexpr int kStopAfterRounds = kNumThreads * kRoundsPerThread / 4;
  while (rounds_done.Value() < kStopAfterRounds) {
    (void)mgr_->GetIncomingSockets();
    (void)mgr_->GetOutgoingSockets();
    absl::SleepFor(absl::Milliseconds(1));
  }

  mgr_->Stop();
  for (auto& tc : threads) tc.join();
  ta.join();

  // Stop() emptied the containers and rejected everything added afterwards.
  EXPECT_TRUE(mgr_->GetIncomingSockets().empty());
  EXPECT_TRUE(mgr_->GetOutgoingSockets().empty());

  // Besides the listeners closed by Start(), no socket may remain open.
  const size_t sockets_after = CountOpenSocketFds();
  EXPECT_EQ(sockets_after, sockets_before - num_peer_endpoints_);
}

}  // namespace
}  // namespace peregrine::internal::testing
