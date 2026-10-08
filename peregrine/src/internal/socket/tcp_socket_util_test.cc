#include "peregrine/src/internal/socket/tcp_socket_util.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
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

class TcpSocketUtilTest : public TestWithParam<SocketTestParam> {
 protected:
  TcpSocketUtilTest()
      : cfg_(GetParam()),
        local_(IpLocalhost(cfg_.family), TestOnly_FindFreeTcpPort(cfg_.family)),
        listener_(TestOnly_CreateTcpSocket(cfg_.family, cfg_.blocking)),
        connector_(TestOnly_CreateTcpSocket(cfg_.family, cfg_.blocking)) {
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

// For non-blocking tcp socket tests, see connector_test.cc.
INSTANTIATE_TEST_SUITE_P(BlockingTcpSocketUtilTest, TcpSocketUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true)),
                         ToString);

TEST_P(TcpSocketUtilTest, ScatterGather) {
  // Create a big chunk of data and a recv buffer.
  constexpr size_t kDataSize = 16UL << 20;
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
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    const size_t kPartial = kDataSize / 2;
    std::vector<IoVec> iovecs = {
        {IoVec(recv_buf.data(), kPartial)},
        {IoVec(recv_buf.data() + kPartial, kDataSize - kPartial)},
    };
    CHECK_OK(
        TcpSocketUtil::Recv(new_socket->fd(), iovecs, absl::InfiniteFuture()));
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(!connector_->Connect(local_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    const size_t kPartial = kDataSize / 3;
    std::vector<IoVec> iovecs = {
        {IoVec(send_buf.data(), kPartial)},
        {IoVec(send_buf.data() + kPartial, kPartial)},
        {IoVec(send_buf.data() + kPartial * 2, kDataSize - kPartial * 2)},
    };
    CHECK_OK(
        TcpSocketUtil::Send(connector_->fd(), iovecs, absl::InfiniteFuture()));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

TEST_P(TcpSocketUtilTest, ExpiredDeadlineFailsImmediately) {
  std::vector<Byte> buf(64, 0x01);
  std::vector<IoVec> iovecs = {{IoVec(buf.data(), buf.size())}};

  absl::Notification server_ready;
  absl::Notification done;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    const int ret = listener_->Accept(cfg_.blocking);
    CHECK_GE(ret, 0);
    const fd_t new_fd(ret);
    auto new_socket = TcpSocket::Create(new_fd, cfg_.family, cfg_.blocking);
    done.WaitForNotification();
  });

  server_ready.WaitForNotification();
  CHECK(!connector_->Connect(local_));
  const absl::Time past = absl::Now() - absl::Milliseconds(10);

  EXPECT_EQ(TcpSocketUtil::Send(connector_->fd(), iovecs, past).code(),
            absl::StatusCode::kInternal);
  EXPECT_EQ(TcpSocketUtil::Recv(connector_->fd(), iovecs, past).code(),
            absl::StatusCode::kInternal);

  done.Notify();
  server.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
