#include "peregrine/src/internal/rdma/rdma_device.h"

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "peregrine/src/internal/rdma/rdma_context.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(RdmaDeviceTest, CreateAndEnumerate) {
  auto dev_or = RdmaDevice::Create();
  if (absl::IsNotFound(dev_or.status())) {
    LOG(INFO) << "No hardware RDMA devices available in this test environment; "
                 "skipping verification.";
    return;
  }

  ASSERT_TRUE(dev_or.ok()) << dev_or.status();
  std::unique_ptr<RdmaDevice> dev = std::move(*dev_or);
  ASSERT_THAT(dev, NotNull());
  EXPECT_FALSE(dev->Contexts().empty());

  for (const auto& ctx : dev->Contexts()) {
    EXPECT_THAT(ctx->GetIbvContext(), NotNull());
    EXPECT_THAT(ctx->GetPd(), NotNull());
    EXPECT_THAT(ctx->GetCq(), NotNull());
    EXPECT_FALSE(ctx->Name().empty());

    const auto& attr = ctx->GetDeviceAttr();
    LOG(INFO) << "[RdmaDevice Verified] Device: " << ctx->Name()
              << " | Max CQE: " << attr.max_cqe << " | Max QP: " << attr.max_qp;

    RdmaContext* found = dev->GetContext(ctx->Name());
    EXPECT_THAT(found, Eq(ctx.get()));
  }

  EXPECT_THAT(dev->GetContext("nonexistent_device_name"), IsNull());
}

}  // namespace
}  // namespace peregrine::internal::testing
