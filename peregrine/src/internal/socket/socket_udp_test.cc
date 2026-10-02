#include "peregrine/src/internal/socket/socket_udp.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
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

TEST_P(UdpSocketTest, ScatterGather) {
  // Create a small chunk of data and a recv buffer.
  constexpr size_t kDataSize = 16UL << 10;
  std::vector<Byte> send_buf(kDataSize);
  std::vector<Byte> recv_buf(kDataSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  // First, create a receiver thread.
  absl::Notification rcvr_ready;
  util::Thread receiver([&]() {
    CHECK(!rskt_->Bind(rcvr_));
    CHECK(!rskt_->Connect(sndr_));
    DCHECK(rskt_->IsConnected());
    std::unique_ptr<IoVecCursor> iovs = CreateIoVecCursor(recv_buf, 2);
    rcvr_ready.Notify();
    if (cfg_.blocking) {
      DCHECK(rskt_->IsBlocking());
      const ssize_t n = rskt_->Recv(*iovs);
      CHECK_GT(n, 0);
      CHECK_LE(n, kDataSize);
    } else {
      // TODO(yongx): add non-blocking test.
    }
  });

  // Second, create a sender thread.
  util::Thread sender([&]() {
    rcvr_ready.WaitForNotification();
    CHECK(!sskt_->Bind(sndr_));
    CHECK(!sskt_->Connect(rcvr_));
    DCHECK(sskt_->IsConnected());
    std::unique_ptr<IoVecCursor> iovs = CreateIoVecCursor(send_buf, 3);
    if (cfg_.blocking) {
      DCHECK(sskt_->IsBlocking());
      CHECK_EQ(sskt_->Send(*iovs), kDataSize);
    } else {
      // TODO(yongx): add non-blocking test.
    }
  });

  // Wait for both threads to finish.
  sender.join();
  receiver.join();

  // Check that the server got the client's message.
  EXPECT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

}  // namespace
}  // namespace peregrine::internal::testing
