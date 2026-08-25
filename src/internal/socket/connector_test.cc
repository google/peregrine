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
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

constexpr bool kTcp = true;

template <int kFamily>
class TcpConnectorTest : public ::testing::Test {
 protected:
  TcpConnectorTest()
      : self_(TestOnly_LocalHostInfoWithZeroDataPlanePorts(kFamily, kTcp)),
        local_(self_.data_plane_listeners[0].GetIpAddr(), /*port=*/0),
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
      std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer, local_);
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

}  // namespace
}  // namespace peregrine::internal::testing
