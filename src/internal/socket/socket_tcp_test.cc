#include "src/internal/socket/socket_tcp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"
#include "src/internal/util/test_util.h"

namespace peregrine::testing {
namespace {

TEST(TcpSocketTest, Move) {
  auto a = TestOnly_CreateTcpSocket(AF_INET);
  EXPECT_GE(a->fd(), 0);

  auto b = std::move(a);
  EXPECT_EQ(a, nullptr);
  EXPECT_GE(b->fd(), 0);
}

template <int kFamily>
class TcpSocketTest : public ::testing::Test {
  static_assert(kFamily == AF_INET || kFamily == AF_INET6);

 protected:
  TcpSocketTest()
      : listen_ip_(kFamily == AF_INET
                       ? IpAddr(ParseIPv4Addr(kIPv4Localhost).value())
                       : IpAddr(ParseIPv6Addr(kIPv6Localhost).value())),
        listen_port_(TestOnly_FindFreeTcpPort(kFamily)),
        listen_socket_(TestOnly_CreateTcpSocket(kFamily)),
        connect_socket_(TestOnly_CreateTcpSocket(kFamily)) {
    CHECK(!listen_socket_->IsConnected());
    CHECK(!connect_socket_->IsConnected());
    CHECK_NE(listen_socket_->fd(), connect_socket_->fd());
  }

 protected:
  const IpAddr listen_ip_;
  const port_t listen_port_;
  const std::unique_ptr<TcpSocket> listen_socket_;
  const std::unique_ptr<TcpSocket> connect_socket_;
};

using TcpIPv4SocketTest = TcpSocketTest<AF_INET>;
using TcpIPv6SocketTest = TcpSocketTest<AF_INET6>;

TEST_F(TcpIPv4SocketTest, SmallMessage) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
  ASSERT_NE(recv_buf, message);

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK_OK(listen_socket_->Listen(listen_ip_, listen_port_));
    server_ready.Notify();
    const auto maybe_new_fd = listen_socket_->Accept();
    CHECK_OK(maybe_new_fd);
    auto new_socket = TcpSocket::Create(maybe_new_fd.value(), AF_INET);
    CHECK(new_socket->IsConnected());
    CHECK(new_socket->IsBlocking());
    CHECK_OK(new_socket->Recv(recv_buf.data(), kMsgSize));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK_OK(connect_socket_->Connect(listen_ip_, listen_port_));
    CHECK(connect_socket_->IsConnected());
    CHECK(connect_socket_->IsBlocking());
    CHECK_OK(connect_socket_->Send(message.data(), kMsgSize));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

TEST_F(TcpIPv6SocketTest, BigData) {
  // Create a big chunk of data and a recv buffer.
  constexpr size_t kDataSize = 1UL << 20;
  std::vector<Byte> send_buf(kDataSize, 0x01);
  std::vector<Byte> recv_buf(kDataSize, 0x02);
  ASSERT_NE(recv_buf, send_buf);

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK_OK(listen_socket_->Listen(listen_ip_, listen_port_));
    server_ready.Notify();
    const auto maybe_new_fd = listen_socket_->Accept();
    CHECK_OK(maybe_new_fd);
    auto new_socket = TcpSocket::Create(maybe_new_fd.value(), AF_INET6);
    CHECK(new_socket->IsConnected());
    CHECK(new_socket->IsBlocking());
    CHECK_OK(new_socket->Recv(recv_buf.data(), kDataSize));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK_OK(connect_socket_->Connect(listen_ip_, listen_port_));
    CHECK(connect_socket_->IsConnected());
    CHECK(connect_socket_->IsBlocking());
    CHECK_OK(connect_socket_->Send(send_buf.data(), kDataSize));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_EQ(recv_buf, send_buf);
}

}  // namespace
}  // namespace peregrine::testing
