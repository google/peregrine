#include "src/internal/rdma/rdma_context.h"

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

TEST(RdmaContextTest, CreateAndEnumerate) {
  auto ctx_or = RdmaContext::Create();
  if (absl::IsNotFound(ctx_or.status())) {
    LOG(INFO) << "No hardware RDMA devices available in this test environment; "
                 "skipping verification.";
    return;
  }
  ASSERT_TRUE(ctx_or.ok()) << ctx_or.status();
  std::unique_ptr<RdmaContext> ctx = std::move(*ctx_or);
  ASSERT_THAT(ctx, NotNull());
  EXPECT_FALSE(ctx->Devices().empty());

  for (const auto& dev_ctx : ctx->Devices()) {
    EXPECT_THAT(dev_ctx->GetDeviceContext(), NotNull());
    EXPECT_THAT(dev_ctx->GetPd(), NotNull());
    EXPECT_THAT(dev_ctx->GetCq(), NotNull());
    EXPECT_FALSE(dev_ctx->Name().empty());

    const auto& attr = dev_ctx->GetDeviceAttr();
    LOG(INFO) << "[RdmaContext Verified] Device: " << dev_ctx->Name()
              << " | Max CQE: " << attr.max_cqe << " | Max QP: " << attr.max_qp;

    RdmaDeviceContext* found = ctx->GetDevice(dev_ctx->Name());
    EXPECT_THAT(found, Eq(dev_ctx.get()));
  }

  EXPECT_THAT(ctx->GetDevice("nonexistent_device_name"), IsNull());
}

}  // namespace
}  // namespace peregrine::internal::testing
