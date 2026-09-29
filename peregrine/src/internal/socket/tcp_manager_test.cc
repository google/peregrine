#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
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
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(mgr_, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }
  static void LongSleep() { absl::SleepFor(absl::Seconds(1)); }

  size_t NumPeers() {
    return std::accumulate(peers_.begin(), peers_.end(), 0,
                           [](size_t sum, const NicInfo& ni) {
                             return sum + ni.endpoints.size();
                           });
  }

  size_t ConnectAll() {
    size_t count = 0;
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        const int ret = mgr_->Connect(/*self=*/{}, peer, cfg_.blocking);
        CHECK_GE(ret, 0) << "failed to connect to " << peer;
        ++count;
      }
    }
    return count;
  }

  size_t DrainAccepted() {
    auto accepted = mgr_->GetIncomingSockets();
    return accepted.size();
  }

  size_t DrainConnected() {
    auto connected = mgr_->GetOutgoingSockets();
    return connected.size();
  }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::unique_ptr<TcpManager> mgr_;
  const std::vector<NicInfo> peers_;
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

  util::Thread tc([&]() { ConnectAll(); });
  tc.join();

  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, ConnectBeforeAccept) {
  util::Thread tc([&]() { ConnectAll(); });
  ShortSleep();

  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  tc.join();

  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, ConcurrentConnects) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  constexpr size_t kNumThreads = 100;
  const size_t num_peers = NumPeers();
  const size_t num_connects = kNumThreads * num_peers;

  std::vector<util::Thread> threads;
  threads.reserve(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      LOG(INFO) << "connecting thread #" << i << "...";
      ConnectAll();
    });
  }
  LongSleep();  // Give threads some time to connect.

  // Drain sockets concurrently with the connecting threads.
  // Each connect produces one accepted and one connected socket.
  size_t count = 0;
  const size_t expected = 2 * num_connects;
  const absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (count < expected && absl::Now() < deadline) {
    count += DrainAccepted();
    count += DrainConnected();
    absl::SleepFor(absl::Milliseconds(1));
  }
  // Draining one side closes the sockets, which might cause the other side
  // to be closed and missed in the subsequent drain operation. Together with
  // potential listen backlog overflows under high concurrency, this race
  // condition justifies a non-100% threshold even on loopback.
  LOG(INFO) << "accepted/connected=" << count << ", expected=" << expected;
  EXPECT_GT(count, expected * 95 / 100);

  for (auto& tc : threads) tc.join();
  mgr_->Stop();
  ta.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
