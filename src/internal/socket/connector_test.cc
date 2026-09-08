#include "src/internal/socket/connector.h"

#include <sys/socket.h>

#include <memory>
#include <string>
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
#include "src/internal/socket/psp/psp_syscall_mock.h"
#include "src/internal/socket/psp/tcp_psp_helper.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

constexpr bool kTcp = true;

const PspToken kPspTokenInvalidKey{.spi = 1, .key = "short"};
const PspToken kPspTokenValid{.spi = 2, .key = std::string(16, 'a')};

template <int kFamily>
class TcpConnectorTest : public ::testing::Test {
 protected:
  TcpConnectorTest()
      : self_(TestOnly_LocalHostInfo(kFamily, kTcp)),
        psp_syscalls_(FakePspTcpSyscalls::Create()),
        acceptor_(TcpAcceptor::Create(self_)),
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(psp_syscalls_, nullptr);
    CHECK_NE(acceptor_, nullptr);
    TestOnly_SetPspTcpSyscalls(psp_syscalls_.get());
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

 protected:
  HostInfo self_;
  const Endpoint local_;
  std::unique_ptr<FakePspTcpSyscalls> psp_syscalls_;
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

TEST_F(TcpConnectorTestIPv6, BindConnectBeforeAccept) {
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

TEST_F(TcpConnectorTestIPv4, CreateUnconnectedAndConnect) {
  std::jthread ta([&]() { acceptor_->Start(Accept); });

  ShortSleep();
  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      CHECK_NE(socket, nullptr);
      EXPECT_FALSE(socket->IsConnected());
      EXPECT_TRUE(TcpConnector::Connect(*socket, peer));
      EXPECT_TRUE(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv6, CreateUnconnectedAndConnect) {
  std::jthread ta([&]() { acceptor_->Start(Accept); });

  ShortSleep();
  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      CHECK_NE(socket, nullptr);
      EXPECT_FALSE(socket->IsConnected());
      EXPECT_TRUE(TcpConnector::Connect(*socket, peer));
      EXPECT_TRUE(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv4, AcquireRxSpiAndKey) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }

  for (const Endpoint& peer : peers_) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::CreateUnconnected(peer);
    ASSERT_NE(socket, nullptr);

    const auto rx = TcpConnector::AcquireRxSpiAndKey(*socket);
    ASSERT_TRUE(rx.ok()) << rx.status();
    EXPECT_TRUE(rx->IsValid());
    EXPECT_NE(rx->spi, 0);
    EXPECT_EQ(rx->key.size(), 16);
  }
}

TEST_F(TcpConnectorTestIPv4, PspConnectSuccess) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }

  std::jthread ta([&]() { acceptor_->Start(Accept); });
  ShortSleep();

  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      ASSERT_NE(socket, nullptr);

      auto self_token = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(self_token.ok()) << self_token.status();

      const PspToken peer_token = kPspTokenValid;
      EXPECT_TRUE(
          TcpConnector::PspConnect(*socket, peer, peer_token, *self_token));
      EXPECT_TRUE(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv6, PspConnectSuccess) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }

  std::jthread ta([&]() { acceptor_->Start(Accept); });
  ShortSleep();

  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      ASSERT_NE(socket, nullptr);

      auto self_token = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(self_token.ok()) << self_token.status();

      const PspToken peer_token = kPspTokenValid;
      EXPECT_TRUE(
          TcpConnector::PspConnect(*socket, peer, peer_token, *self_token));
      EXPECT_TRUE(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv4, PspConnectInvalidServerKey) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }

  std::jthread ta([&]() { acceptor_->Start(Accept); });
  ShortSleep();

  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      ASSERT_NE(socket, nullptr);

      auto self_token = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(self_token.ok()) << self_token.status();

      const PspToken invalid_peer_token = kPspTokenInvalidKey;
      EXPECT_FALSE(TcpConnector::PspConnect(*socket, peer, invalid_peer_token,
                                            *self_token));
      EXPECT_FALSE(socket->IsConnected());
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv4, PspConnectRxSpiMismatch) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }

  std::jthread ta([&]() { acceptor_->Start(Accept); });
  ShortSleep();

  std::jthread tc([&]() {
    for (const Endpoint& peer : peers_) {
      std::unique_ptr<TcpSocket> socket =
          TcpConnector::CreateUnconnected(peer);
      ASSERT_NE(socket, nullptr);

      auto self_token = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(self_token.ok()) << self_token.status();

      const PspToken peer_token = kPspTokenValid;

      // Corrupt self token SPI to induce mismatch.
      PspToken mismatched_self_token = *self_token;
      mismatched_self_token.spi ^= 0xFFFFFFFF;

      EXPECT_FALSE(TcpConnector::PspConnect(*socket, peer, peer_token,
                                            mismatched_self_token));
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

}  // namespace
}  // namespace peregrine::internal::testing
