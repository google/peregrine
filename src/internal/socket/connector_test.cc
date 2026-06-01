#include "src/internal/socket/connector.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

template <int kFamily>
class TcpConnectorTest : public ::testing::Test {
 protected:
  TcpConnectorTest()
      : local_(TestOnly_LocalEndpoint(kFamily, /*tcp=*/true)),
        peer_(local_),
        acceptor_(TcpAcceptor::Create(local_)) {
    CHECK_EQ(local_, peer_);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

 protected:
  const Endpoint local_;
  const Endpoint peer_;
  std::unique_ptr<TcpAcceptor> acceptor_;
};

using TcpConnectorTestIPv4 = TcpConnectorTest<AF_INET>;
using TcpConnectorTestIPv6 = TcpConnectorTest<AF_INET6>;

TEST_F(TcpConnectorTestIPv4, AcceptBeforeConnect) {
  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start();
  });

  ShortSleep();
  std::jthread tc([&]() {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer_);
    DCHECK(socket->IsConnected());
    DCHECK(socket->IsBlocking());
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpConnectorTestIPv6, ConnectBeforeAccept) {
  std::jthread tc([&]() {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer_);
    DCHECK(socket->IsConnected());
    DCHECK(socket->IsBlocking());
  });

  ShortSleep();
  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start();
  });

  ShortSleep();
  acceptor_->Stop();
}

}  // namespace
}  // namespace peregrine::internal::testing
