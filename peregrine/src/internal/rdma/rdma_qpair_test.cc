#include "peregrine/src/internal/rdma/rdma_qpair.h"

#include <infiniband/verbs.h>

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "peregrine/src/internal/rdma/rdma_context.h"
#include "peregrine/src/internal/rdma/rdma_device.h"

namespace peregrine::internal::testing {
namespace {

TEST(RdmaQPairTest, NullDeviceContext) {
  auto qp_or = RdmaQPair::Create(nullptr);
  EXPECT_FALSE(qp_or.ok());
  EXPECT_EQ(qp_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RdmaQPairTest, LifecycleAndCreation) {
  auto dev_or = RdmaDevice::Create();
  if (!dev_or.ok() || (*dev_or)->Contexts().empty()) {
    GTEST_SKIP() << "No physical RDMA hw contexts available on this host: "
                 << dev_or.status();
  }

  RdmaContext* ctx = (*dev_or)->Contexts()[0].get();
  ASSERT_NE(ctx, nullptr);

  auto qp_or = RdmaQPair::Create(ctx);
  ASSERT_TRUE(qp_or.ok()) << qp_or.status();
  std::unique_ptr<RdmaQPair> qp = std::move(*qp_or);

  // Newly created QP is automatically in INIT state (ready to connect, but not
  // yet in RTS)
  EXPECT_FALSE(qp->IsConnected());
  EXPECT_GT(qp->Qpn(), 0);
  EXPECT_NE(qp->GetQp(), nullptr);
  EXPECT_NE(qp->GetSendCq(), nullptr);
  EXPECT_NE(qp->GetRecvCq(), nullptr);
  EXPECT_EQ(qp->GetDeviceContext(), ctx);

  auto gid_or = qp->GetLocalGid();
  EXPECT_TRUE(gid_or.ok());
}

TEST(RdmaQPairTest, LoopbackConnection) {
  auto dev_or = RdmaDevice::Create();
  if (!dev_or.ok() || (*dev_or)->Contexts().empty()) {
    GTEST_SKIP() << "No physical RDMA hw contexts available on this host: "
                 << dev_or.status();
  }

  RdmaContext* ctx = (*dev_or)->Contexts()[0].get();
  ASSERT_NE(ctx, nullptr);

  auto qp1_or = RdmaQPair::Create(ctx);
  ASSERT_TRUE(qp1_or.ok()) << qp1_or.status();
  std::unique_ptr<RdmaQPair> qp1 = std::move(*qp1_or);

  auto qp2_or = RdmaQPair::Create(ctx);
  ASSERT_TRUE(qp2_or.ok()) << qp2_or.status();
  std::unique_ptr<RdmaQPair> qp2 = std::move(*qp2_or);

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
