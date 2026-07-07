#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/control/message.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::TestParamInfo;
using ::testing::Values;

using Param = std::tuple</*family=*/int>;

std::string ToString(const TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrFormat("IPv%d", family == AF_INET ? 4 : 6);
}

class ControlTest : public ::testing::TestWithParam<Param> {
 protected:
  ControlTest()
      : family_(std::get<0>(GetParam())),
        tmp_(ConnectedChannelPair::CreateTcp(family_)),
        local_(std::move(tmp_.sndr)),
        remote_(std::move(tmp_.rcvr)) {}

 protected:
  const int family_;
  ConnectedChannelPair tmp_;
  Control local_;
  Control remote_;
};

INSTANTIATE_TEST_SUITE_P(, ControlTest,
                         /*family=*/Values(AF_INET, AF_INET6), ToString);

TEST_P(ControlTest, SendRecv) {
  constexpr std::string_view kPeer = "127.0.0.1:56789";
  constexpr uint64_t kLaddr = 0x1000;
  constexpr uint64_t kRaddr = 0x2000;
  constexpr uint32_t kLen = 300;

  // Send a message.
  proto::ReqMsg msg_s;
  auto prs = msg_s.mutable_peer_requests();
  prs->set_peer(kPeer);
  auto rs = prs->add_requests();
  rs->set_op(proto::Request::READ);
  rs->set_laddr(kLaddr);
  rs->set_raddr(kRaddr);
  rs->set_len(kLen);
  ASSERT_TRUE(local_.EnqueueSend(msg_s));
  LOG(INFO) << "msg sent";

  // Receive the message.
  proto::ReqMsg msg_r;
  ASSERT_FALSE(msg_r.has_peer_requests());
  while (!remote_.DequeueRecv(msg_r)) {
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_TRUE(msg_r.has_peer_requests());
  LOG(INFO) << "msg rcvd";

  // Check the message.
  const proto::PeerRequests& prr = msg_r.peer_requests();
  EXPECT_EQ(prr.peer(), kPeer);
  EXPECT_EQ(prr.requests_size(), 1);
  EXPECT_TRUE(Message::AreEqual(prr.requests(0), *rs));
}

}  // namespace
}  // namespace peregrine::internal::testing
