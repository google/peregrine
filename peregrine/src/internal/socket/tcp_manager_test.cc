#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/socket.h>

#include <memory>
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
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(mgr_, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

  void ConnectAll() {
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        const int ret = mgr_->Connect(/*self=*/{}, peer, cfg_.blocking);
        CHECK_GE(ret, 0) << "failed to connect to " << peer;
        CHECK(getConnected()) << "failed to get connected socket for " << peer;
      }
    }
  }

 private:
  bool getConnected() {
    const absl::Time deadline = absl::Now() + absl::Seconds(10);
    std::unique_ptr<TcpSocket> a = nullptr;
    std::unique_ptr<TcpSocket> b = nullptr;
    while (a == nullptr || b == nullptr) {
      if (a == nullptr) a = GetOneIncomingSocket(*mgr_);
      if (b == nullptr) b = GetOneOutgoingSocket(*mgr_);
      absl::SleepFor(absl::Milliseconds(100));
      if (absl::Now() > deadline) break;
    }
    return a != nullptr && b != nullptr;
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

  // All connections must be done before stopping the manager.
  tc.join();
  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, ConnectBeforeAccept) {
  util::Thread tc([&]() { ConnectAll(); });

  ShortSleep();
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });

  // All connections must be done before stopping the manager.
  tc.join();
  mgr_->Stop();
  ta.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
