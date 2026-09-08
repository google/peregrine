#include "src/internal/socket/connector.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/psp/psp_mock.h"
#include "src/internal/socket/psp/psp_util.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

constexpr bool kTcp = true;

template <int kFamily>
class TcpConnectorTest : public ::testing::Test {
 protected:
  TcpConnectorTest()
      : self_(TestOnly_LocalHostInfo(kFamily, kTcp)),
        acceptor_(TcpAcceptor::Create(self_)),
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(acceptor_, nullptr);
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

 protected:
  HostInfo self_;
  const Endpoint local_;
  std::unique_ptr<TcpAcceptor> acceptor_;
  const std::vector<Endpoint> peers_;
};

using TcpConnectorTestIPv4 = TcpConnectorTest<AF_INET>;
using TcpConnectorTestIPv6 = TcpConnectorTest<AF_INET6>;

TEST_F(TcpConnectorTestIPv4, AcceptBeforeConnect) {
  std::jthread ta([&]() {
    acceptor_->Start(Accept);
  });

  ShortSleep();
  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv6, ConnectBeforeAccept) {
  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
    }
  });

  ShortSleep();
  std::jthread ta([&]() {
    acceptor_->Start(Accept);
  });

  ShortSleep();
  acceptor_->Stop();
}

template <int kFamily>
class PspTcpConnectorTest : public TcpConnectorTest<kFamily> {
 protected:
  PspTcpConnectorTest()
      : psp_syscalls_(psp::testing::FakePspTcpSyscalls::Create()),
        psp_xchg_func_([this](const PspToken& self_token,
                              const Endpoint& peer_target,
                              const Endpoint& peer_control) {
          return this->acceptor_->ExchangePspTokens(self_token, peer_target);
        }) {
    CHECK_NE(psp_syscalls_, nullptr);
    psp::TestOnly_SetPspTcpSyscalls(psp_syscalls_.get());
  }

  ~PspTcpConnectorTest() override { psp::TestOnly_SetPspTcpSyscalls(nullptr); }

 protected:
  std::unique_ptr<psp::testing::FakePspTcpSyscalls> psp_syscalls_;
  TcpConnector::PspTokenExchangeFunc psp_xchg_func_;
};

using PspTcpConnectorTestIPv4 = PspTcpConnectorTest<AF_INET>;
using PspTcpConnectorTestIPv6 = PspTcpConnectorTest<AF_INET6>;

TEST_F(PspTcpConnectorTestIPv4, AcceptBeforeConnect) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  std::jthread ta([&]() { acceptor_->Start(Accept); });

  ShortSleep();
  std::jthread tc([&]() {
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const Endpoint& peer_target : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreatePsp(peer_target, peer_control, psp_xchg_func_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(PspTcpConnectorTestIPv6, ConnectBeforeAccept) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  std::jthread tc([&]() {
    const Endpoint& peer_control = self_.control_plane_listener;
    for (const Endpoint& peer_target : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreatePsp(peer_target, peer_control, psp_xchg_func_);
      CHECK_NE(socket, nullptr);
      DCHECK(socket->IsBlocking());
      DCHECK(socket->IsConnected());
      DCHECK(psp::IsPspEnabled(socket->fd()));
    }
  });

  ShortSleep();
  std::jthread ta([&]() { acceptor_->Start(Accept); });

  ShortSleep();
  acceptor_->Stop();
}

}  // namespace
}  // namespace peregrine::internal::testing
