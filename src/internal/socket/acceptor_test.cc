#include "src/internal/socket/acceptor.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

template <int kFamily>
class TcpAcceptorTest : public ::testing::Test {
 protected:
  TcpAcceptorTest()
      : local_(TestOnly_LocalEndpoint(kFamily, /*tcp=*/true)),
        acceptor_(TcpAcceptor::Create(local_)) {
    CHECK_NE(acceptor_, nullptr);
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(300)); }

 protected:
  const Endpoint local_;
  std::unique_ptr<TcpAcceptor> acceptor_;
};

using TcpAcceptorTestIPv4 = TcpAcceptorTest<AF_INET>;
using TcpAcceptorTestIPv6 = TcpAcceptorTest<AF_INET6>;

TEST_F(TcpAcceptorTestIPv4, StartThenStop) {
  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start(Accept);
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpAcceptorTestIPv6, StopThenStart) {
  acceptor_->Stop();

  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start(Accept);
  });
}

TEST(TcpAcceptorTest, EphemeralPortBindingIPv4) {
  const Endpoint ep = Endpoint::Create("127.0.0.1:0");
  auto acceptor = TcpAcceptor::Create(ep);
  ASSERT_NE(acceptor, nullptr);
  EXPECT_GT(acceptor->BoundEndpoint().Port(), 0);
  EXPECT_EQ(acceptor->BoundEndpoint().GetIpAddr().ToString(), "127.0.0.1");
}

TEST(TcpAcceptorTest, EphemeralPortBindingIPv6) {
  const Endpoint ep = Endpoint::Create("[::1]:0");
  auto acceptor = TcpAcceptor::Create(ep);
  ASSERT_NE(acceptor, nullptr);
  EXPECT_GT(acceptor->BoundEndpoint().Port(), 0);
  EXPECT_EQ(acceptor->BoundEndpoint().GetIpAddr().ToString(), "::1");
}

}  // namespace
}  // namespace peregrine::internal::testing
