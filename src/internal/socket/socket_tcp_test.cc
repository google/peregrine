#include "src/internal/socket/socket_tcp.h"

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
#include "src/internal/util/test_util.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

template <int kFamily>
class TcpSocketTest : public ::testing::Test {
  static_assert(kFamily == AF_INET || kFamily == AF_INET6);

 protected:
  TcpSocketTest()
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

using TcpIPv4SocketTest = TcpSocketTest<AF_INET>;
using TcpIPv6SocketTest = TcpSocketTest<AF_INET6>;

TEST_F(TcpIPv4SocketTest, SmallMessage) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
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
    CHECK_EQ(new_socket->Recv(recv_buf.data(), kMsgSize), kMsgSize);
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(connector_->Connect(local_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    CHECK_EQ(connector_->Send(message.data(), kMsgSize), kMsgSize);
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's message.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), message));
}

TEST_F(TcpIPv6SocketTest, BigData) {
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
    constexpr int kRN = 2;
    constexpr size_t kPartial = kDataSize / kRN;
    const struct iovec recv_iov[kRN] = {
        {.iov_base = (void*)recv_buf.data(), .iov_len = kPartial},
        {.iov_base = (void*)(recv_buf.data() + kPartial),
         .iov_len = kDataSize - kPartial},
    };
    CHECK_EQ(new_socket->RecvV(recv_iov), kDataSize);
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    const Endpoint local_ip(local_.GetIpAddr(), 0);
    CHECK(connector_->Bind(local_ip));
    CHECK(connector_->Connect(local_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    constexpr int kSN = 3;
    constexpr size_t kPartial = kDataSize / kSN;
    const struct iovec send_iov[kSN] = {
        {.iov_base = (void*)send_buf.data(), .iov_len = kPartial},
        {.iov_base = (void*)(send_buf.data() + kPartial), .iov_len = kPartial},
        {.iov_base = (void*)(send_buf.data() + 2 * kPartial),
         .iov_len = kDataSize - 2 * kPartial},
    };
    CHECK_EQ(connector_->SendV(send_iov), kDataSize);
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

}  // namespace
}  // namespace peregrine::internal::testing
