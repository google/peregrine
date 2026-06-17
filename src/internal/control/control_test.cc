#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/channel/channel_test_util.h"

namespace peregrine::internal::testing {
namespace {

using TestParams = std::tuple</*family=*/int>;

std::string ToString(const ::testing::TestParamInfo<TestParams>& info) {
  const int family = std::get<0>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrCat("IPv", family == AF_INET ? "4" : "6");
}

class ControlTest : public ::testing::TestWithParam<TestParams> {
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
                         ::testing::Combine(::testing::Values(AF_INET,
                                                              AF_INET6)),
                         ToString);

TEST_P(ControlTest, SendRecv) {
  constexpr uint64_t kLaddr = 0x1000;
  constexpr uint64_t kRaddr = 0x2000;
  constexpr uint32_t kLen = 300;

  // Send a message.
  proto::Control msg;
  auto r = msg.mutable_request();
  r->set_op(proto::Request::READ);
  r->set_laddr(kLaddr);
  r->set_raddr(kRaddr);
  r->set_len(kLen);
  ASSERT_TRUE(local_.EnqueueSend(msg));
  LOG(INFO) << "msg sent";

  // Receive the message.
  proto::Control msg2;
  ASSERT_FALSE(msg2.has_request());
  while (!remote_.DequeueRecv(msg2)) {
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_TRUE(msg2.has_request());
  LOG(INFO) << "msg rcvd";

  // Check the message.
  const proto::Request& r2 = msg2.request();
  EXPECT_EQ(r2.op(), proto::Request::READ);
  EXPECT_EQ(r2.laddr(), kLaddr);
  EXPECT_EQ(r2.raddr(), kRaddr);
  EXPECT_EQ(r2.len(), kLen);
  LOG(INFO) << "test done";
}

}  // namespace
}  // namespace peregrine::internal::testing
