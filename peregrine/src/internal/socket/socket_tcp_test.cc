#include "peregrine/src/internal/socket/socket_tcp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_test_util.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

// Connection establishment is always blocking to make the code simpler.
// After that, socket blocking mode is set according to `cfg_.blocking`.
class TcpSocketTest : public TestWithParam<SocketTestParam> {
 protected:
  TcpSocketTest()
      : cfg_(GetParam()),
        local_(IpLocalhost(cfg_.family), TestOnly_FindFreeTcpPort(cfg_.family)),
        listener_(TestOnly_CreateTcpSocket(cfg_.family, /*blocking=*/true)),
        connector_(TestOnly_CreateTcpSocket(cfg_.family, /*blocking=*/true)) {
    DCHECK(listener_->IsValid());
    DCHECK(connector_->IsValid());
    DCHECK(!listener_->IsConnected());
    DCHECK(!connector_->IsConnected());
    DCHECK_NE(listener_->fd(), connector_->fd());
  }

 protected:
  const SocketTestConfig cfg_;
  const Endpoint local_;
  const std::unique_ptr<TcpSocket> listener_;
  const std::unique_ptr<TcpSocket> connector_;
};

INSTANTIATE_TEST_SUITE_P(, TcpSocketTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(TcpSocketTest, ScatterGather) {
  // Create some data to send and a buffer to receive it.
  constexpr ssize_t kDataSize = 128UL << 20;
  std::vector<Byte> send_buf(kDataSize);
  std::vector<Byte> recv_buf(kDataSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  // First, create a server thread.
  absl::Notification server_ready;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const int ret = listener_->Accept(cfg_.blocking);
    CHECK_GE(ret, 0);

    const fd_t new_fd(ret);
    auto new_socket = TcpSocket::Create(new_fd, cfg_.family, cfg_.blocking);
    DCHECK(new_socket->IsConnected());
    DCHECK(new_socket->MatchesBlocking());
    auto iovs = CreateIoVecCursor(recv_buf, /*splits=*/2);
    if (cfg_.blocking) {
      CHECK(new_socket->IsBlocking());
      CHECK_EQ(new_socket->Recv(*iovs), kDataSize);
    } else {
      CHECK(new_socket->IsNonBlocking());
      ssize_t rcvd = 0;
      while (true) {
        const ssize_t bytes = new_socket->Recv(*iovs);
        if (bytes < 0) break;
        if (rcvd += bytes; rcvd >= kDataSize) break;
        if (bytes == 0) absl::SleepFor(absl::Milliseconds(10));
      }
      CHECK_EQ(rcvd, kDataSize);
    }
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    const Endpoint local_ip(local_.GetIpAddr(), 0);
    CHECK(!connector_->Bind(local_ip));
    CHECK(!connector_->Connect(local_));
    DCHECK(connector_->IsConnected());
    auto iovs = CreateIoVecCursor(send_buf, /*splits=*/3);
    if (cfg_.blocking) {
      CHECK(connector_->IsBlocking());
      CHECK_EQ(connector_->Send(*iovs), kDataSize);
    } else {
      CHECK(!connector_->SetBlocking(false));
      CHECK(connector_->IsNonBlocking());
      ssize_t sent = 0;
      while (true) {
        const ssize_t bytes = connector_->Send(*iovs);
        if (bytes < 0) break;
        if (sent += bytes; sent >= kDataSize) break;
        if (bytes == 0) absl::SleepFor(absl::Milliseconds(10));
      }
      CHECK_EQ(sent, kDataSize);
    }
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the recv buffer matches the send buffer.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

}  // namespace
}  // namespace peregrine::internal::testing
