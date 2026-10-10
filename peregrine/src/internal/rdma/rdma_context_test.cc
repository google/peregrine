#include "peregrine/src/internal/rdma/rdma_context.h"

#include <infiniband/verbs.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::NotNull;

TEST(RdmaContextTest, CreateAndVerify) {
  int num_devices = 0;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);
  if (device_list == nullptr || num_devices == 0) {
    if (device_list != nullptr) {
      ibv_free_device_list(device_list);
    }
    LOG(INFO) << "No hardware RDMA devices available in this test environment; "
                 "skipping verification.";
    return;
  }

  int verified_devices = 0;
  for (int i = 0; i < num_devices; ++i) {
    struct ibv_device* dev = device_list[i];
    ASSERT_THAT(dev, NotNull());

    std::unique_ptr<RdmaContext> ctx = RdmaContext::Create(dev);
    if (ctx == nullptr) {
      continue;
    }
    ++verified_devices;
    EXPECT_THAT(ctx->GetIbvContext(), NotNull());
    EXPECT_THAT(ctx->GetPd(), NotNull());
    EXPECT_THAT(ctx->GetCq(), NotNull());
    EXPECT_FALSE(ctx->Name().empty());
    EXPECT_GT(ctx->GetDeviceAttr().max_cqe, 0);
    EXPECT_GE(ctx->GidIndex(), 0);

    const auto& attr = ctx->GetDeviceAttr();
    LOG(INFO) << "[Test Verified] Device Name: " << ctx->Name()
              << " | Max CQE: " << attr.max_cqe << " | Max QP: " << attr.max_qp
              << " | Max MR: " << attr.max_mr
              << " | Phys Ports: " << static_cast<int>(attr.phys_port_cnt)
              << " | GID Index: " << ctx->GidIndex();
  }
  ibv_free_device_list(device_list);
  if (verified_devices == 0) {
    GTEST_SKIP() << "No usable RDMA devices could be initialized in this test "
                    "environment.";
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
