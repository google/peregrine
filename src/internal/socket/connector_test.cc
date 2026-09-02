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
#include "src/internal/socket/psp/psp_syscall_mock.h"  // NOLINT
#include "src/internal/socket/psp/tcp_psp_helper.h"
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

    auto rx_key = TcpConnector::AcquireRxSpiAndKey(*socket);
    ASSERT_TRUE(rx_key.ok()) << rx_key.status();
    EXPECT_TRUE(rx_key->IsValid());
    EXPECT_NE(rx_key->spi, 0);
    EXPECT_EQ(rx_key->key.size(), 16);
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

      auto client_key = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(client_key.ok()) << client_key.status();

      const PspSpiKey server_key = {
          .spi = 0x55667788,
          .key = std::string(16, 's'),
      };

      EXPECT_TRUE(
          TcpConnector::PspConnect(*socket, peer, server_key, *client_key));
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

      auto client_key = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(client_key.ok()) << client_key.status();

      const PspSpiKey server_key = {
          .spi = 0x55667788,
          .key = std::string(16, 's'),
      };

      EXPECT_TRUE(
          TcpConnector::PspConnect(*socket, peer, server_key, *client_key));
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

      auto client_key = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(client_key.ok()) << client_key.status();

      // Server key size is not 16 bytes.
      const PspSpiKey invalid_server_key = {
          .spi = 0x55667788,
          .key = "invalid_len",
      };

      EXPECT_FALSE(TcpConnector::PspConnect(*socket, peer, invalid_server_key,
                                            *client_key));
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

      auto client_key = TcpConnector::AcquireRxSpiAndKey(*socket);
      ASSERT_TRUE(client_key.ok()) << client_key.status();

      const PspSpiKey server_key = {
          .spi = 0x55667788,
          .key = std::string(16, 's'),
      };

      // Corrupt client_key SPI to induce mismatch.
      PspSpiKey mismatched_client_key = *client_key;
      mismatched_client_key.spi ^= 0xFFFFFFFF;

      EXPECT_FALSE(TcpConnector::PspConnect(*socket, peer, server_key,
                                            mismatched_client_key));
    }
  });

  ShortSleep();
  acceptor_->Stop();
}

}  // namespace
}  // namespace peregrine::internal::testing
