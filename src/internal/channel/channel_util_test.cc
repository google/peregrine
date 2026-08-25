#include "src/internal/channel/channel_util.h"

#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

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

constexpr bool kTcp = true;

class ChannelUtilTest : public ::testing::TestWithParam<Param> {
 protected:
  ChannelUtilTest()
      : family_(std::get<0>(GetParam())),
        self_(TestOnly_LocalHostInfoWithZeroDataPlanePorts(family_, kTcp)),
        acceptor_(TcpAcceptor::Create(self_)),
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(acceptor_, nullptr);
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

 protected:
  const int family_;
  HostInfo self_;
  std::unique_ptr<TcpAcceptor> acceptor_;
  const std::vector<Endpoint> peers_;
};

INSTANTIATE_TEST_SUITE_P(, ChannelUtilTest,
                         /*family=*/Values(AF_INET, AF_INET6), ToString);

TEST_P(ChannelUtilTest, Create) {
  std::jthread ta([&]() {
    acceptor_->Start(Accept);
  });

  for (const Endpoint& peer : peers_) {
    constexpr int kNumChannels = 8;
    Channels chs = Create(peer, kNumChannels);
    EXPECT_EQ(chs.size(), kNumChannels);
  }

  acceptor_->Stop();
}

}  // namespace
}  // namespace peregrine::internal::testing
