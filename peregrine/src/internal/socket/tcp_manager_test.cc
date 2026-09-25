#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/psp/psp_mock.h"
#include "peregrine/src/internal/socket/psp/psp_util.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

using ::absl::StatusCode::kNotFound;
using ::absl_testing::StatusIs;
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

class PspTcpManagerTest : public TcpManagerTest {
 protected:
  PspTcpManagerTest()
      : psp_syscalls_(psp::testing::FakePspTcpSyscalls::Create()),
        psp_token_xchg_([this](const PspToken& self_token, const Endpoint& peer,
                               const Endpoint& peer_control) {
          return this->mgr_->ExchangePspTokens(self_token, peer);
        }) {
    CHECK_NE(psp_syscalls_, nullptr);
    psp::TestOnly_SetPspTcpSyscalls(psp_syscalls_.get());
  }

  ~PspTcpManagerTest() override { psp::TestOnly_SetPspTcpSyscalls(nullptr); }

 protected:
  std::unique_ptr<psp::testing::FakePspTcpSyscalls> psp_syscalls_;
  TcpManager::PspTokenExchange psp_token_xchg_;
};

INSTANTIATE_TEST_SUITE_P(, PspTcpManagerTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*gen_blocking=*/Values(true, false)),
                         ToString);

TEST_P(PspTcpManagerTest, HandlePspTokenExchange) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  ASSERT_FALSE(self_.data_plane_listeners.empty());
  const PspToken peer_token(Spi(1), Gen(9), {0xbe, 0xef});
  const NicInfo& nic = self_.data_plane_listeners[0];
  const Endpoint& self_target = nic.endpoints[0];
  const auto self_token = mgr_->ExchangePspTokens(peer_token, self_target);
  ASSERT_TRUE(self_token.ok());
  EXPECT_TRUE(self_token->IsValid());

  const Endpoint unknown_target = Endpoint::Create("127.0.0.1:9999");
  const auto status = mgr_->ExchangePspTokens(peer_token, unknown_target);
  EXPECT_THAT(status, StatusIs(kNotFound));
}

TEST_P(PspTcpManagerTest, AcceptBeforeConnect) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  util::Thread ta([&]() { mgr_->Start(OnAccept, cfg_.blocking); });

  ShortSleep();
  util::Thread tc([&]() {
    const Endpoint self = {};
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const NicInfo& ni : peers_) {
      const Endpoint& peer = ni.endpoints[0];
      std::unique_ptr<TcpSocket> socket =
          TcpManager::ConnectPsp(self, peer, peer_control, psp_token_xchg_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
    }
  });

  ShortSleep();
  mgr_->Stop();
  ta.join();
  tc.join();
}

TEST_P(PspTcpManagerTest, ConnectBeforeAccept) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  util::Thread tc([&]() {
    const Endpoint self = {};
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const NicInfo& ni : peers_) {
      const Endpoint& peer = ni.endpoints[0];
      std::unique_ptr<TcpSocket> socket =
          TcpManager::ConnectPsp(self, peer, peer_control, psp_token_xchg_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
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
