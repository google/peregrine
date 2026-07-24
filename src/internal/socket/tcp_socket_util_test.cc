#include "src/internal/socket/tcp_socket_util.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <thread>  // NOLINT
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

template <int kFamily>
class TcpSocketUtilTest : public ::testing::Test {
  static_assert(kFamily == AF_INET || kFamily == AF_INET6);

 protected:
  TcpSocketUtilTest()
      : local_(kFamily == AF_INET ? IPv4Localhost() : IPv6Localhost(),
               TestOnly_FindFreeTcpPort(kFamily)),
        listener_(TestOnly_CreateTcpSocket(kFamily)),
        connector_(TestOnly_CreateTcpSocket(kFamily)) {
    DCHECK(listener_->IsValid());
    DCHECK(connector_->IsValid());
    DCHECK(!listener_->IsConnected());
    DCHECK(!connector_->IsConnected());
    DCHECK_NE(listener_->fd(), connector_->fd());
  }

 protected:
  const Endpoint local_;
  const std::unique_ptr<TcpSocket> listener_;
  const std::unique_ptr<TcpSocket> connector_;
};

using TcpIPv4SocketUtilTest = TcpSocketUtilTest<AF_INET>;
using TcpIPv6SocketUtilTest = TcpSocketUtilTest<AF_INET6>;

TEST_F(TcpIPv4SocketUtilTest, SmallMessage) {
  // Create a small send message and a recv buffer.
  const size_t kMsgSize = 64UL << 10;
  std::vector<Byte> message(kMsgSize);
  std::vector<Byte> recv_buf(kMsgSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(message));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), message));

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK(listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const fd_t new_fd = listener_->Accept();

    CHECK_GE(new_fd.value(), 0);
    auto new_socket = TcpSocket::Create(new_fd, AF_INET);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    CHECK_OK(TcpSocketUtil::Recv(new_socket->fd(), recv_buf.data(), kMsgSize));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(connector_->Connect(local_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    CHECK_OK(TcpSocketUtil::Send(connector_->fd(), message.data(), kMsgSize));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's message.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), message));
}

TEST_F(TcpIPv6SocketUtilTest, BigData) {
  // Create a big chunk of data and a recv buffer.
  constexpr size_t kDataSize = 16UL << 20;
  std::vector<Byte> send_buf(kDataSize);
  std::vector<Byte> recv_buf(kDataSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK(listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const fd_t new_fd = listener_->Accept();

    CHECK_GE(new_fd.value(), 0);
    auto new_socket = TcpSocket::Create(new_fd, AF_INET6);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    const size_t kPartial = kDataSize / 2;
    std::vector<IoVec> iovecs = {
        {IoVec(recv_buf.data(), kPartial)},
        {IoVec(recv_buf.data() + kPartial, kDataSize - kPartial)},
    };
    CHECK_OK(TcpSocketUtil::RecvV(new_socket->fd(), iovecs));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(connector_->Connect(local_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    const size_t kPartial = kDataSize / 3;
    std::vector<IoVec> iovecs = {
        {IoVec(send_buf.data(), kPartial)},
        {IoVec(send_buf.data() + kPartial, kPartial)},
        {IoVec(send_buf.data() + kPartial * 2, kDataSize - kPartial * 2)},
    };
    CHECK_OK(TcpSocketUtil::SendV(connector_->fd(), iovecs));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

}  // namespace
}  // namespace peregrine::internal::testing
