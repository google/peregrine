#include "src/internal/rdma/rdma_queue_pair.h"

#include <infiniband/verbs.h>

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/internal/rdma/rdma_device_manager.h"

namespace peregrine::internal::testing {
namespace {

TEST(RdmaQueuePairTest, NullDeviceContext) {
  auto qp_or = RdmaQueuePair::Create(nullptr);
  EXPECT_FALSE(qp_or.ok());
  EXPECT_EQ(qp_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RdmaQueuePairTest, LifecycleAndCreation) {
  auto dev_mgr_or = RdmaDeviceManager::Create();
  if (!dev_mgr_or.ok() || (*dev_mgr_or)->Devices().empty()) {
    GTEST_SKIP() << "No physical RDMA hardware devices available on this host: "
                 << dev_mgr_or.status();
  }

  RdmaDeviceContext* dev_ctx = (*dev_mgr_or)->Devices()[0].get();
  ASSERT_NE(dev_ctx, nullptr);

  auto qp_or = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp_or.ok()) << qp_or.status();
  std::unique_ptr<RdmaQueuePair> qp = std::move(*qp_or);

  // Newly created QP is automatically in INIT state (ready to connect, but not
  // yet in RTS)
  EXPECT_FALSE(qp->IsConnected());
  EXPECT_GT(qp->Qpn(), 0);
  EXPECT_NE(qp->GetQp(), nullptr);
  EXPECT_EQ(qp->GetDeviceContext(), dev_ctx);

  auto gid_or = qp->GetLocalGid();
  EXPECT_TRUE(gid_or.ok());
}

TEST(RdmaQueuePairTest, LoopbackConnection) {
  auto dev_mgr_or = RdmaDeviceManager::Create();
  if (!dev_mgr_or.ok() || (*dev_mgr_or)->Devices().empty()) {
    GTEST_SKIP() << "No physical RDMA hardware devices available on this host: "
                 << dev_mgr_or.status();
  }

  RdmaDeviceContext* dev_ctx = (*dev_mgr_or)->Devices()[0].get();
  ASSERT_NE(dev_ctx, nullptr);

  auto qp1_or = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp1_or.ok()) << qp1_or.status();
  std::unique_ptr<RdmaQueuePair> qp1 = std::move(*qp1_or);

  auto qp2_or = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp2_or.ok()) << qp2_or.status();
  std::unique_ptr<RdmaQueuePair> qp2 = std::move(*qp2_or);

  auto gid_or = qp1->GetLocalGid();
  ASSERT_TRUE(gid_or.ok()) << gid_or.status();
  const union ibv_gid local_gid = *gid_or;

  EXPECT_FALSE(qp1->IsConnected());
  EXPECT_FALSE(qp2->IsConnected());

  // Connect both QPs in loopback
  ASSERT_TRUE(qp1->Connect(qp2->Qpn(), local_gid).ok());
  ASSERT_TRUE(qp2->Connect(qp1->Qpn(), local_gid).ok());

  EXPECT_TRUE(qp1->IsConnected());
  EXPECT_TRUE(qp2->IsConnected());
}

}  // namespace
}  // namespace peregrine::internal::testing
