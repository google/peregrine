#include "src/internal/socket/acceptor.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/psp/psp_mock.h"
#include "src/internal/socket/psp/psp_util.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

constexpr bool kTcp = true;

template <int kFamily>
class TcpAcceptorTest : public ::testing::Test {
 protected:
  TcpAcceptorTest()
      : local_(TestOnly_LocalHostInfo(kFamily, kTcp)),
        acceptor_(TcpAcceptor::Create(local_)) {
    CHECK(local_.IsValid());
    CHECK_NE(acceptor_, nullptr);
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(300)); }

 protected:
  HostInfo local_;
  std::unique_ptr<TcpAcceptor> acceptor_;
};

using TcpAcceptorTestIPv4 = TcpAcceptorTest<AF_INET>;
using TcpAcceptorTestIPv6 = TcpAcceptorTest<AF_INET6>;

TEST_F(TcpAcceptorTestIPv4, StartThenStop) {
  std::jthread ta([&]() {
    acceptor_->Start(Accept);
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpAcceptorTestIPv6, StopThenStart) {
  acceptor_->Stop();

  std::jthread ta([&]() {
    acceptor_->Start(Accept);
  });
}

template <int kFamily>
class PspTcpAcceptorTest : public TcpAcceptorTest<kFamily> {
 protected:
  PspTcpAcceptorTest()
      : psp_syscalls_(psp::testing::FakePspTcpSyscalls::Create()) {
    CHECK_NE(psp_syscalls_, nullptr);
    psp::TestOnly_SetPspTcpSyscalls(psp_syscalls_.get());
  }

  ~PspTcpAcceptorTest() override { psp::TestOnly_SetPspTcpSyscalls(nullptr); }

 protected:
  std::unique_ptr<psp::testing::FakePspTcpSyscalls> psp_syscalls_;
};

using PspTcpAcceptorTestIPv4 = PspTcpAcceptorTest<AF_INET>;
using PspTcpAcceptorTestIPv6 = PspTcpAcceptorTest<AF_INET6>;

TEST_F(PspTcpAcceptorTestIPv6, HandlePspTokenExchange) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  ASSERT_FALSE(local_.data_plane_listeners.empty());
  const PspToken peer_token(Spi(1), Gen(9), {0xbe, 0xef});
  const Endpoint self_target = local_.data_plane_listeners[0];
  const auto self_token = acceptor_->ExchangePspTokens(peer_token, self_target);
  ASSERT_TRUE(self_token.ok());
  EXPECT_TRUE(self_token->IsValid());

  const Endpoint unknown_target = Endpoint::Create("127.0.0.1:9999");
  const auto status = acceptor_->ExchangePspTokens(peer_token, unknown_target);
  EXPECT_THAT(status, ::absl_testing::StatusIs(::absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace peregrine::internal::testing
