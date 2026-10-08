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
#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
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

using ::absl::StatusCode::kInternal;
using ::absl_testing::StatusIs;
using ::testing::Combine;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;
using ::testing::Not;
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

  void SetSocketBufferSizeTiny() {
    // Accepted socket inherits the buffer size from the listener.
    const int size = 1024;
    const int lfd = listener_->fd().value();
    const int cfd = connector_->fd().value();
    CHECK_EQ(setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)), 0);
    CHECK_EQ(setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)), 0);
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
    CHECK_OK(TcpSocketUtil::Recv(new_socket->fd(), iovecs, /*timeout_ms=*/-1));
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
    CHECK_OK(TcpSocketUtil::Send(connector_->fd(), iovecs, /*timeout_ms=*/-1));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

TEST_P(TcpSocketUtilTest, Timeout) {
  constexpr size_t kDataSize = 64UL << 10;
  std::vector<Byte> buf(kDataSize, 0x01);
  std::vector<IoVec> iovecs = {{IoVec(buf.data(), buf.size())}};

  // To ensure sender blocks, set the socket buffer size to a small value.
  SetSocketBufferSizeTiny();

  // Create a server thread, which neither sends nor receives any data.
  absl::Notification server_ready;
  absl::Notification done;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    const int ret = listener_->Accept(cfg_.blocking);
    CHECK_GE(ret, 0);
    auto new_socket = TcpSocket::Create(fd_t(ret), cfg_.family, cfg_.blocking);
    // Keep the connection open until the client is done.
    done.WaitForNotification();
  });

  server_ready.WaitForNotification();
  CHECK(!connector_->Connect(local_));
  // A zero timeout never blocks; a positive timeout blocks until it expires.
  for (const int timeout_ms : {0, 10}) {
    EXPECT_THAT(TcpSocketUtil::Recv(connector_->fd(), iovecs, timeout_ms),
                StatusIs(kInternal, HasSubstr("recvmsg timeout")))
        << "timeout_ms=" << timeout_ms;
    EXPECT_THAT(TcpSocketUtil::Send(connector_->fd(), iovecs, timeout_ms),
                StatusIs(kInternal, HasSubstr("sendmsg timeout")))
        << "timeout_ms=" << timeout_ms;
  }
  done.Notify();

  server.join();
}

TEST_P(TcpSocketUtilTest, PeerResetIsNotTimeout) {
  constexpr size_t kDataSize = 64UL << 10;
  constexpr size_t kTinySize = 64UL;
  std::vector<Byte> send_buf(kDataSize, 0x01);
  std::vector<Byte> recv_buf(kTinySize, 0x00);

  // To ensure sender blocks, set the socket buffer size to a small value.
  SetSocketBufferSizeTiny();

  // Create a server thread to read a tiny amount of the data, then closes
  // the connection with some data unread, which resets the connection.
  absl::Notification server_ready;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    const int ret = listener_->Accept(cfg_.blocking);
    CHECK_GE(ret, 0);
    auto new_socket = TcpSocket::Create(fd_t(ret), cfg_.family, cfg_.blocking);
    std::vector<IoVec> iovecs = {{IoVec(recv_buf.data(), recv_buf.size())}};
    CHECK_OK(TcpSocketUtil::Recv(new_socket->fd(), iovecs, /*timeout_ms=*/-1));
  });

  // The send fails due to the reset, well before its timeout expires.
  server_ready.WaitForNotification();
  CHECK(!connector_->Connect(local_));
  std::vector<IoVec> iovecs = {{IoVec(send_buf.data(), send_buf.size())}};
  EXPECT_THAT(TcpSocketUtil::Send(connector_->fd(), iovecs,
                                  /*timeout_ms=*/60'000),
              StatusIs(kInternal, Not(HasSubstr("timeout"))));

  server.join();
}

}  // namespace
}  // namespace peregrine::internal::testing
