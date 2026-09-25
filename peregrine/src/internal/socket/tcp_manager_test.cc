#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/socket.h>

#include <memory>
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
#include "peregrine/src/internal/socket/socket_tcp.h"
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

  static void OnAccept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::unique_ptr<TcpManager> mgr_;
  const std::vector<NicInfo> peers_;
};

INSTANTIATE_TEST_SUITE_P(, TcpManagerTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*gen_blocking=*/Values(true, false)),
                         ToString);

TEST_P(TcpManagerTest, StartThenStop) {
  util::Thread ta([&]() { mgr_->Start(OnAccept, cfg_.blocking); });

  ShortSleep();
  mgr_->Stop();
  ta.join();
}

TEST_P(TcpManagerTest, StopThenStart) {
  mgr_->Stop();

  util::Thread ta([&]() { mgr_->Start(OnAccept, cfg_.blocking); });
  ta.join();
}

TEST_P(TcpManagerTest, AcceptBeforeConnect) {
  util::Thread ta([&]() { mgr_->Start(OnAccept, cfg_.blocking); });

  ShortSleep();
  util::Thread tc([&]() {
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        mgr_->Connect(/*self=*/{}, peer, /*blocking=*/true);
        for (auto& socket : mgr_->GetConnected()) {
          CHECK_NE(socket, nullptr);
          DCHECK(socket->IsBlocking());
          DCHECK(socket->IsConnected());
        }
      }
    }
  });

  ShortSleep();
  mgr_->Stop();
  ta.join();
  tc.join();
}

TEST_P(TcpManagerTest, ConnectBeforeAccept) {
  util::Thread tc([&]() {
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        mgr_->Connect(/*self=*/{}, peer, /*blocking=*/true);
        for (auto& socket : mgr_->GetConnected()) {
          CHECK_NE(socket, nullptr);
          DCHECK(socket->IsBlocking());
          DCHECK(socket->IsConnected());
        }
      }
    }
  });

  ShortSleep();
  util::Thread ta([&]() { mgr_->Start(OnAccept, cfg_.blocking); });

  ShortSleep();
  mgr_->Stop();
  tc.join();
  ta.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
