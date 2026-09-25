#include "peregrine/src/internal/channel/channel_util.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_types.h"
#include "peregrine/src/internal/rdma/rdma_device_context.h"
#include "peregrine/src/internal/rdma/rdma_device_manager.h"
#include "peregrine/src/internal/rdma/rdma_queue_pair.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
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

class ChannelUtilTest : public TestWithParam<SocketTestParam> {
 protected:
  ChannelUtilTest()
      : cfg_(GetParam()),
        self_(TestOnly_LocalHostInfo(cfg_.family, /*tcp=*/true)),
        mgr_(TcpManager::Create(self_)),
        peers_(self_.data_plane_listeners) {
    CHECK(self_.IsValid());
    CHECK_NE(mgr_, nullptr);
  }

  static void Accept(std::unique_ptr<TcpSocket> socket) {
    auto x = std::move(socket);
    CHECK_NE(x, nullptr);
  }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::unique_ptr<TcpManager> mgr_;
  const std::vector<NicInfo> peers_;
};

INSTANTIATE_TEST_SUITE_P(, ChannelUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*gen_blocking=*/Values(true)),
                         ToString);

TEST_P(ChannelUtilTest, Create) {
  util::Thread ta([&]() { mgr_->Start(Accept, cfg_.blocking); });

  const Endpoint self = {};
  constexpr int kNumChannels = 2;
  for (const NicInfo& nic : peers_) {
    for (const Endpoint& peer : nic.endpoints) {
      auto chs = Create(*mgr_, self, peer, cfg_.blocking, kNumChannels);
      EXPECT_EQ(chs.size(), kNumChannels);
    }
  }

  mgr_->Stop();
  ta.join();
}

TEST(ChannelUtilNonParamTest, CreateRdmaChannel) {
  auto dev_mgr = RdmaDeviceManager::Create();
  if (!dev_mgr.ok() || dev_mgr.value()->Devices().empty()) {
    GTEST_SKIP() << "No RDMA hardware devices found on this host.";
  }
  RdmaDeviceContext* const dev_ctx = dev_mgr.value()->Devices()[0].get();
  auto qp_or = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp_or.ok());
  auto ch = CreateRdmaChannel(std::move(*qp_or), 0x1234, 0x5678);
  ASSERT_NE(ch, nullptr);
  EXPECT_EQ(ch->Type(), ChannelType::kReliableMessage);
}

}  // namespace
}  // namespace peregrine::internal::testing
