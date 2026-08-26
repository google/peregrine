#include "src/internal/rdma/rdma_device_manager.h"

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/internal/rdma/rdma_device_context.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(RdmaDeviceManagerTest, CreateAndEnumerate) {
  auto dev_mgr_or = RdmaDeviceManager::Create();
  if (absl::IsNotFound(dev_mgr_or.status())) {
    LOG(INFO) << "No hardware RDMA devices available in this test environment; "
                 "skipping verification.";
    return;
  }
  ASSERT_TRUE(dev_mgr_or.ok()) << dev_mgr_or.status();
  std::unique_ptr<RdmaDeviceManager> dev_mgr = std::move(*dev_mgr_or);
  ASSERT_THAT(dev_mgr, NotNull());
  EXPECT_FALSE(dev_mgr->Devices().empty());

  for (const auto& dev_ctx : dev_mgr->Devices()) {
    EXPECT_THAT(dev_ctx->GetDeviceContext(), NotNull());
    EXPECT_THAT(dev_ctx->GetPd(), NotNull());
    EXPECT_THAT(dev_ctx->GetCq(), NotNull());
    EXPECT_FALSE(dev_ctx->Name().empty());

    const auto& attr = dev_ctx->GetDeviceAttr();
    LOG(INFO) << "[RdmaDeviceManager Verified] Device: " << dev_ctx->Name()
              << " | Max CQE: " << attr.max_cqe << " | Max QP: " << attr.max_qp;

    RdmaDeviceContext* found = dev_mgr->GetDevice(dev_ctx->Name());
    EXPECT_THAT(found, Eq(dev_ctx.get()));
  }

  EXPECT_THAT(dev_mgr->GetDevice("nonexistent_device_name"), IsNull());
}

}  // namespace
}  // namespace peregrine::internal::testing
