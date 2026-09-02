#include "src/internal/socket/acceptor.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/socket/psp/psp_syscall_mock.h"  // NOLINT
#include "src/internal/socket/psp/tcp_psp_helper.h"
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

TEST_F(TcpAcceptorTestIPv4, HandlePspKeyExchange) {
  if (!IsPspSupported()) {
    GTEST_SKIP() << "PSP is not supported";
  }
  ASSERT_FALSE(local_.data_plane_listeners.empty());
  const Endpoint target = local_.data_plane_listeners[0];
  const PspSpiKey valid_client_key = {
      .spi = 0x12345678,
      .key = std::string(16, 'a'),
  };

  // 1. Successful key exchange with explicit target endpoint.
  auto server_key_or =
      acceptor_->HandlePspKeyExchange(target, valid_client_key);
  ASSERT_TRUE(server_key_or.ok()) << server_key_or.status();
  EXPECT_TRUE(server_key_or->IsValid());

  // 2. Successful key exchange without endpoint (when single listener).
  if (local_.data_plane_listeners.size() == 1) {
    auto default_key_or =
        acceptor_->HandlePspKeyExchange(Endpoint(), valid_client_key);
    ASSERT_TRUE(default_key_or.ok()) << default_key_or.status();
    EXPECT_TRUE(default_key_or->IsValid());
  }

  // 3. Invalid client key (zero SPI).
  EXPECT_EQ(acceptor_->HandlePspKeyExchange(
                target, {.spi = 0, .key = std::string(16, 'a')})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // 4. Invalid client key (invalid size).
  EXPECT_EQ(acceptor_->HandlePspKeyExchange(
                target, {.spi = 0x12345678, .key = "short"})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // 5. Unknown target endpoint.
  const Endpoint unknown_target = Endpoint::Create("127.0.0.1:9999");
  EXPECT_EQ(acceptor_->HandlePspKeyExchange(unknown_target, valid_client_key)
                .status()
                .code(),
            absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace peregrine::internal::testing
