#include "src/internal/socket/socket_udp.h"

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

namespace peregrine::internal::testing {
namespace {

TEST(UdpSocketTest, Move) {
  auto a = TestOnly_CreateUdpSocket(AF_INET);
  EXPECT_GE(a->fd(), 0);

  auto b = std::move(a);
  EXPECT_EQ(a, nullptr);
  EXPECT_GE(b->fd(), 0);
}

template <int kFamily>
class UdpSocketTest : public ::testing::Test {
 protected:
  UdpSocketTest()
      : ip_(kFamily == AF_INET ? IpAddr(ParseIPv4Addr(kIPv4Localhost).value())
                               : IpAddr(ParseIPv6Addr(kIPv6Localhost).value())),
        send_port_(TestOnly_FindFreeUdpPort(kFamily)),
        recv_port_(TestOnly_FindFreeUdpPort(kFamily)),
        send_socket_(TestOnly_CreateUdpSocket(kFamily)),
        recv_socket_(TestOnly_CreateUdpSocket(kFamily)) {
    CHECK_NE(send_port_, recv_port_);
    CHECK(recv_socket_->Bind(ip_, recv_port_));
    CHECK(send_socket_->Bind(ip_, send_port_));
    CHECK(!send_socket_->IsConnected());
    CHECK(!recv_socket_->IsConnected());
    CHECK_NE(send_socket_->fd(), recv_socket_->fd());
  }

 protected:
  const IpAddr ip_;
  const port_t send_port_;
  const port_t recv_port_;
  const std::unique_ptr<UdpSocket> send_socket_;
  const std::unique_ptr<UdpSocket> recv_socket_;
};

using UdpSocketIPv4Test = UdpSocketTest<AF_INET>;
using UdpSocketIPv6Test = UdpSocketTest<AF_INET6>;

TEST_F(UdpSocketIPv4Test, SendRecv) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
  ASSERT_NE(recv_buf, message);

  // First, create a receiver thread.
  absl::Notification rcvr_ready;
  std::thread rcvr([&]() {
    CHECK(recv_socket_->Connect(ip_, send_port_));
    CHECK(recv_socket_->IsConnected());
    CHECK(recv_socket_->IsBlocking());
    rcvr_ready.Notify();
    CHECK(recv_socket_->Recv(recv_buf.data(), kMsgSize));
  });

  // Second, create a sender thread.
  std::thread sndr([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(send_socket_->Connect(ip_, recv_port_));
    CHECK(send_socket_->IsConnected());
    CHECK(send_socket_->IsBlocking());
    CHECK(send_socket_->Send(message.data(), kMsgSize));
  });

  // Wait for both threads to finish.
  sndr.join();
  rcvr.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

TEST_F(UdpSocketIPv6Test, ScatterGather) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
  ASSERT_NE(recv_buf, message);

  // First, create a receiver thread.
  absl::Notification rcvr_ready;
  std::thread rcvr([&]() {
    CHECK(recv_socket_->Connect(ip_, send_port_));
    CHECK(recv_socket_->IsConnected());
    CHECK(recv_socket_->IsBlocking());
    constexpr int kRN = 2;
    const struct iovec recv_iov[kRN] = {
        {.iov_base = (void*)recv_buf.data(), .iov_len = 2},
        {.iov_base = (void*)(recv_buf.data() + 2), .iov_len = kMsgSize - 2},
    };
    rcvr_ready.Notify();
    CHECK(recv_socket_->RecvV(recv_iov, kRN, kMsgSize));
  });

  // Second, create a sender thread.
  std::thread sndr([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(send_socket_->Connect(ip_, recv_port_));
    CHECK(send_socket_->IsConnected());
    CHECK(send_socket_->IsBlocking());
    constexpr int kSN = 2;
    const struct iovec send_iov[kSN] = {
        {.iov_base = (void*)message.data(), .iov_len = 1},
        {.iov_base = (void*)(message.data() + 1), .iov_len = kMsgSize - 1},
    };
    CHECK(send_socket_->SendV(send_iov, kSN, kMsgSize));
  });

  // Wait for both threads to finish.
  sndr.join();
  rcvr.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

}  // namespace
}  // namespace peregrine::internal::testing
