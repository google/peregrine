#include "src/internal/socket/socket_udp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/util/test_util.h"
#include "src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

constexpr bool kBlocking = true;

template <int kFamily>
class UdpSocketTest : public ::testing::Test {
 protected:
  UdpSocketTest()
      : sndr_(kFamily == AF_INET ? IPv4Localhost() : IPv6Localhost(),
              TestOnly_FindFreeUdpPort(kFamily)),
        rcvr_(kFamily == AF_INET ? IPv4Localhost() : IPv6Localhost(),
              TestOnly_FindFreeUdpPort(kFamily)),
        sskt_(TestOnly_CreateUdpSocket(kFamily, kBlocking)),
        rskt_(TestOnly_CreateUdpSocket(kFamily, kBlocking)) {
    CHECK_NE(sndr_.Port(), rcvr_.Port());
    DCHECK(sskt_->IsValid());
    DCHECK(rskt_->IsValid());
    DCHECK(!sskt_->IsConnected());
    DCHECK(!rskt_->IsConnected());
    DCHECK_NE(sskt_->fd(), rskt_->fd());
  }

 protected:
  const Endpoint sndr_;
  const Endpoint rcvr_;
  const std::unique_ptr<UdpSocket> sskt_;
  const std::unique_ptr<UdpSocket> rskt_;
};

using BlockingUdpSocketIPv4Test = UdpSocketTest<AF_INET>;
using BlockingUdpSocketIPv6Test = UdpSocketTest<AF_INET6>;

TEST_F(BlockingUdpSocketIPv4Test, SendRecv) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
  ASSERT_NE(recv_buf, message);

  // First, create a receiver thread.
  absl::Notification rcvr_ready;
  util::Thread receiver([&]() {
    CHECK(!rskt_->Bind(rcvr_));
    CHECK(!rskt_->Connect(sndr_));
    DCHECK(rskt_->IsBlocking());
    DCHECK(rskt_->IsConnected());
    rcvr_ready.Notify();
    const ssize_t n = rskt_->Recv(recv_buf.data(), kMsgSize);
    CHECK_GT(n, 0);
    CHECK_LE(n, kMsgSize);
  });

  // Second, create a sender thread.
  util::Thread sender([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(!sskt_->Bind(sndr_));
    CHECK(!sskt_->Connect(rcvr_));
    DCHECK(sskt_->IsBlocking());
    DCHECK(sskt_->IsConnected());
    CHECK_EQ(sskt_->Send(message.data(), kMsgSize), kMsgSize);
  });

  // Wait for both threads to finish.
  sender.join();
  receiver.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

TEST_F(BlockingUdpSocketIPv6Test, ScatterGather) {
  // Create a small send message and a recv buffer.
  const std::vector<Byte> message = {'h', 'e', 'l', 'l', 'o'};
  const size_t kMsgSize = message.size();
  std::vector<Byte> recv_buf(kMsgSize, 0);
  ASSERT_NE(recv_buf, message);

  // First, create a receiver thread.
  absl::Notification rcvr_ready;
  util::Thread receiver([&]() {
    CHECK(!rskt_->Bind(rcvr_));
    CHECK(!rskt_->Connect(sndr_));
    DCHECK(rskt_->IsBlocking());
    DCHECK(rskt_->IsConnected());
    constexpr int kRN = 2;
    const struct iovec recv_iov[kRN] = {
        {.iov_base = (void*)recv_buf.data(), .iov_len = 2},
        {.iov_base = (void*)(recv_buf.data() + 2), .iov_len = kMsgSize - 2},
    };
    rcvr_ready.Notify();
    const ssize_t n = rskt_->RecvV(recv_iov);
    CHECK_GT(n, 0);
    CHECK_LE(n, kMsgSize);
  });

  // Second, create a sender thread.
  util::Thread sender([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(!sskt_->Bind(sndr_));
    CHECK(!sskt_->Connect(rcvr_));
    DCHECK(sskt_->IsBlocking());
    DCHECK(sskt_->IsConnected());
    constexpr int kSN = 2;
    const struct iovec send_iov[kSN] = {
        {.iov_base = (void*)message.data(), .iov_len = 1},
        {.iov_base = (void*)(message.data() + 1), .iov_len = kMsgSize - 1},
    };
    CHECK_EQ(sskt_->SendV(send_iov), kMsgSize);
  });

  // Wait for both threads to finish.
  sender.join();
  receiver.join();

  // Check that the server got the client's message.
  EXPECT_EQ(recv_buf, message);
}

}  // namespace
}  // namespace peregrine::internal::testing
