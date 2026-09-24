#include "src/internal/socket/connector.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/nicinfo.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/psp/psp_mock.h"
#include "src/internal/socket/psp/psp_util.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"
#include "src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::Values;

using Param = std::tuple</*family=*/int>;

std::string ToString(const ::testing::TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  return absl::StrFormat("IPv%d", family == AF_INET ? 4 : 6);
}

class TcpConnectorTest : public ::testing::TestWithParam<Param> {
 protected:
  TcpConnectorTest()
      : family_(std::get<0>(GetParam())),
        self_(TestOnly_LocalHostInfo(family_, /*tcp=*/true)),
        acceptor_(TcpAcceptor::Create(self_)),
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(acceptor_, nullptr);
  }

  static void OnAccept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

 protected:
  const int family_;
  HostInfo self_;
  std::unique_ptr<TcpAcceptor> acceptor_;
  const std::vector<NicInfo> peers_;
};

INSTANTIATE_TEST_SUITE_P(, TcpConnectorTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6)),
                         ToString);

TEST_P(TcpConnectorTest, AcceptBeforeConnect) {
  util::Thread ta([&]() { acceptor_->Start(OnAccept); });

  ShortSleep();
  util::Thread tc([&]() {
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        std::unique_ptr<TcpSocket> socket =
            TcpConnector::Create(/*self=*/{}, peer);
        CHECK_NE(socket, nullptr);
        DCHECK(socket->IsBlocking());
        DCHECK(socket->IsConnected());
      }
    }
  });

  ShortSleep();
  acceptor_->Stop();
  ta.join();
  tc.join();
}

TEST_P(TcpConnectorTest, ConnectBeforeAccept) {
  util::Thread tc([&]() {
    for (const NicInfo& ni : peers_) {
      for (const Endpoint& peer : ni.endpoints) {
        std::unique_ptr<TcpSocket> socket =
            TcpConnector::Create(/*self=*/{}, peer);
        CHECK_NE(socket, nullptr);
        DCHECK(socket->IsBlocking());
        DCHECK(socket->IsConnected());
      }
    }
  });

  ShortSleep();
  util::Thread ta([&]() { acceptor_->Start(OnAccept); });

  ShortSleep();
  acceptor_->Stop();
  tc.join();
  ta.join();
}

class PspTcpConnectorTest : public TcpConnectorTest {
 protected:
  PspTcpConnectorTest()
      : psp_syscalls_(psp::testing::FakePspTcpSyscalls::Create()),
        psp_xchg_func_([this](const PspToken& self_token, const Endpoint& peer,
                              const Endpoint& peer_control) {
          return this->acceptor_->ExchangePspTokens(self_token, peer);
        }) {
    CHECK_NE(psp_syscalls_, nullptr);
    psp::TestOnly_SetPspTcpSyscalls(psp_syscalls_.get());
  }

  ~PspTcpConnectorTest() override { psp::TestOnly_SetPspTcpSyscalls(nullptr); }

 protected:
  std::unique_ptr<psp::testing::FakePspTcpSyscalls> psp_syscalls_;
  TcpConnector::PspTokenExchangeFunc psp_xchg_func_;
};

INSTANTIATE_TEST_SUITE_P(, PspTcpConnectorTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6)),
                         ToString);

TEST_P(PspTcpConnectorTest, AcceptBeforeConnect) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  util::Thread ta([&]() { acceptor_->Start(OnAccept); });

  ShortSleep();
  util::Thread tc([&]() {
    const Endpoint self = {};
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const NicInfo& ni : peers_) {
      const Endpoint& peer = ni.endpoints[0];
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreatePsp(self, peer, peer_control, psp_xchg_func_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
    }
  });

  ShortSleep();
  acceptor_->Stop();
  ta.join();
  tc.join();
}

TEST_P(PspTcpConnectorTest, ConnectBeforeAccept) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  util::Thread tc([&]() {
    const Endpoint self = {};
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const NicInfo& ni : peers_) {
      const Endpoint& peer = ni.endpoints[0];
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreatePsp(self, peer, peer_control, psp_xchg_func_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
    }
  });

  ShortSleep();
  util::Thread ta([&]() { acceptor_->Start(OnAccept); });

  ShortSleep();
  acceptor_->Stop();
  tc.join();
  ta.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
