#include "peregrine/src/internal/socket/acceptor.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
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

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

class TcpAcceptorTest : public TestWithParam<SocketTestParam> {
 protected:
  TcpAcceptorTest()
      : cfg_(GetParam()),
        self_(TestOnly_LocalHostInfo(cfg_.family, /*tcp=*/true)),
        acceptor_(TcpAcceptor::Create(self_)) {
    CHECK(self_.IsValid());
    CHECK_NE(acceptor_, nullptr);
  }

  static void OnAccept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(300)); }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::unique_ptr<TcpAcceptor> acceptor_;
};

INSTANTIATE_TEST_SUITE_P(, TcpAcceptorTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*gen_blocking=*/Values(true, false)),
                         ToString);

TEST_P(TcpAcceptorTest, StartThenStop) {
  util::Thread ta([&]() { acceptor_->Start(OnAccept, cfg_.blocking); });

  ShortSleep();
  acceptor_->Stop();
  ta.join();
}

TEST_P(TcpAcceptorTest, StopThenStart) {
  acceptor_->Stop();

  util::Thread ta([&]() { acceptor_->Start(OnAccept, cfg_.blocking); });
  ta.join();
}

class PspTcpAcceptorTest : public TcpAcceptorTest {
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

INSTANTIATE_TEST_SUITE_P(, PspTcpAcceptorTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*gen_blocking=*/Values(true, false)),
                         ToString);

TEST_P(PspTcpAcceptorTest, HandlePspTokenExchange) {
  if (!psp::IsPspSupported()) {
    GTEST_SKIP() << "psp not supported";
  }

  ASSERT_FALSE(self_.data_plane_listeners.empty());
  const PspToken peer_token(Spi(1), Gen(9), {0xbe, 0xef});
  const NicInfo& nic = self_.data_plane_listeners[0];
  const Endpoint& self_target = nic.endpoints[0];
  const auto self_token = acceptor_->ExchangePspTokens(peer_token, self_target);
  ASSERT_TRUE(self_token.ok());
  EXPECT_TRUE(self_token->IsValid());

  const Endpoint unknown_target = Endpoint::Create("127.0.0.1:9999");
  const auto status = acceptor_->ExchangePspTokens(peer_token, unknown_target);
  EXPECT_THAT(status, ::absl_testing::StatusIs(::absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace peregrine::internal::testing
