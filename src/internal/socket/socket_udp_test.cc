#include "src/internal/socket/socket_udp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

template <int kFamily>
class UdpSocketTest : public ::testing::Test {
 protected:
  UdpSocketTest()
      : sndr_ip_(kFamily == AF_INET ? IPv4Localhost() : IPv6Localhost()),
        rcvr_ip_(sndr_ip_),
        sndr_port_(TestOnly_FindFreeUdpPort(kFamily)),
        rcvr_port_(TestOnly_FindFreeUdpPort(kFamily)),
        sndr_(TestOnly_CreateUdpSocket(kFamily)),
        rcvr_(TestOnly_CreateUdpSocket(kFamily)) {
    CHECK_NE(sndr_port_, rcvr_port_);
    DCHECK(sndr_->IsValid());
    DCHECK(rcvr_->IsValid());
    DCHECK(!sndr_->IsConnected());
    DCHECK(!rcvr_->IsConnected());
    DCHECK_NE(sndr_->fd(), rcvr_->fd());
  }

 protected:
  const IpAddr sndr_ip_;
  const IpAddr rcvr_ip_;
  const port_t sndr_port_;
  const port_t rcvr_port_;
  const std::unique_ptr<UdpSocket> sndr_;
  const std::unique_ptr<UdpSocket> rcvr_;
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
  std::thread receiver([&]() {
    CHECK(rcvr_->Bind(rcvr_ip_, rcvr_port_));
    CHECK(rcvr_->Connect(sndr_ip_, sndr_port_));
    DCHECK(rcvr_->IsConnected());
    rcvr_ready.Notify();
    DCHECK(rcvr_->IsBlocking());
    CHECK(rcvr_->Recv(recv_buf.data(), kMsgSize));
  });

  // Second, create a sender thread.
  std::thread sender([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(sndr_->Bind(sndr_ip_, sndr_port_));
    CHECK(sndr_->Connect(rcvr_ip_, rcvr_port_));
    DCHECK(sndr_->IsConnected());
    DCHECK(sndr_->IsBlocking());
    CHECK(sndr_->Send(message.data(), kMsgSize));
  });

  // Wait for both threads to finish.
  sender.join();
  receiver.join();

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
  std::thread receiver([&]() {
    CHECK(rcvr_->Bind(rcvr_ip_, rcvr_port_));
    CHECK(rcvr_->Connect(sndr_ip_, sndr_port_));
    DCHECK(rcvr_->IsConnected());
    constexpr int kRN = 2;
    const struct iovec recv_iov[kRN] = {
        {.iov_base = (void*)recv_buf.data(), .iov_len = 2},
        {.iov_base = (void*)(recv_buf.data() + 2), .iov_len = kMsgSize - 2},
    };
    rcvr_ready.Notify();
    DCHECK(rcvr_->IsBlocking());
    CHECK(rcvr_->RecvV(recv_iov, kRN, kMsgSize));
  });

  // Second, create a sender thread.
  std::thread sender([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(sndr_->Bind(sndr_ip_, sndr_port_));
    CHECK(sndr_->Connect(rcvr_ip_, rcvr_port_));
    DCHECK(sndr_->IsConnected());
    constexpr int kSN = 2;
    const struct iovec send_iov[kSN] = {
        {.iov_base = (void*)message.data(), .iov_len = 1},
        {.iov_base = (void*)(message.data() + 1), .iov_len = kMsgSize - 1},
    };
    DCHECK(sndr_->IsBlocking());
    CHECK(sndr_->SendV(send_iov, kSN, kMsgSize));
  });

  // Wait for both threads to finish.
  sender.join();
  receiver.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

}  // namespace
}  // namespace peregrine::internal::testing
