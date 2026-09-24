#include "peregrine/src/internal/socket/socket_udp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

class UdpSocketTest : public TestWithParam<SocketTestParam> {
 protected:
  UdpSocketTest()
      : cfg_(GetParam()),
        sndr_(IpLocalhost(cfg_.family), TestOnly_FindFreeUdpPort(cfg_.family)),
        rcvr_(IpLocalhost(cfg_.family), TestOnly_FindFreeUdpPort(cfg_.family)),
        sskt_(TestOnly_CreateUdpSocket(cfg_.family, cfg_.blocking)),
        rskt_(TestOnly_CreateUdpSocket(cfg_.family, cfg_.blocking)) {
    CHECK_NE(sndr_.Port(), rcvr_.Port());
    DCHECK(sskt_->IsValid());
    DCHECK(rskt_->IsValid());
    DCHECK(!sskt_->IsConnected());
    DCHECK(!rskt_->IsConnected());
    DCHECK_NE(sskt_->fd(), rskt_->fd());
  }

 protected:
  const SocketTestConfig cfg_;
  const Endpoint sndr_;
  const Endpoint rcvr_;
  const std::unique_ptr<UdpSocket> sskt_;
  const std::unique_ptr<UdpSocket> rskt_;
};

INSTANTIATE_TEST_SUITE_P(BlockingUdpSocketTest, UdpSocketTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true)),
                         ToString);

TEST_P(UdpSocketTest, SendRecv) {
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

TEST_P(UdpSocketTest, ScatterGather) {
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
