#include "src/internal/rdma/rdma_device_context.h"

#include <infiniband/verbs.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::NotNull;

TEST(RdmaDeviceContextTest, CreateAndVerify) {
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

  for (int i = 0; i < num_devices; ++i) {
    struct ibv_device* dev = device_list[i];
    ASSERT_THAT(dev, NotNull());

    std::unique_ptr<RdmaDeviceContext> dev_ctx = RdmaDeviceContext::Create(dev);
    ASSERT_THAT(dev_ctx, NotNull());
    EXPECT_THAT(dev_ctx->GetDeviceContext(), NotNull());
    EXPECT_THAT(dev_ctx->GetPd(), NotNull());
    EXPECT_THAT(dev_ctx->GetCq(), NotNull());
    EXPECT_FALSE(dev_ctx->Name().empty());
    EXPECT_GT(dev_ctx->GetDeviceAttr().max_cqe, 0);
    EXPECT_GE(dev_ctx->GidIndex(), 0);

    const auto& attr = dev_ctx->GetDeviceAttr();
    LOG(INFO) << "[Test Verified] Device Name: " << dev_ctx->Name()
              << " | Max CQE: " << attr.max_cqe << " | Max QP: " << attr.max_qp
              << " | Max MR: " << attr.max_mr
              << " | Phys Ports: " << static_cast<int>(attr.phys_port_cnt)
              << " | GID Index: " << dev_ctx->GidIndex();
  }

  ibv_free_device_list(device_list);
}

}  // namespace
}  // namespace peregrine::internal::testing
