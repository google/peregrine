#include "peregrine/src/internal/rdma/rdma_memory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "infiniband/verbs.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "peregrine/src/internal/rdma/rdma_device.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

class RdmaMemoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto dev_or = RdmaDevice::Create();
    if (absl::IsNotFound(dev_or.status())) {
      GTEST_SKIP()
          << "No hardware RDMA devices available in this test environment.";
    }
    ASSERT_TRUE(dev_or.ok()) << dev_or.status();
    dev_ = std::move(*dev_or);
    ASSERT_THAT(dev_, NotNull());
    ASSERT_FALSE(dev_->Contexts().empty());
  }

  std::unique_ptr<RdmaDevice> dev_;
};

TEST_F(RdmaMemoryTest, RegisterAndLookupBaseBuffer) {
  RdmaMemory mem(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& ctx : dev_->Contexts()) {
    struct ibv_mr* mr =
        mem.GetMemoryRegion(buffer.data(), buffer.size(), ctx->Name());
    ASSERT_THAT(mr, NotNull());
    EXPECT_THAT(mr->addr, Eq(buffer.data()));
    EXPECT_THAT(mr->length, Eq(buffer.size()));

    auto lkey_or = mem.GetLKey(buffer.data(), buffer.size(), ctx->Name());
    ASSERT_TRUE(lkey_or.ok());
    EXPECT_THAT(*lkey_or, Eq(mr->lkey));

    auto rkey_or = mem.GetRKey(buffer.data(), buffer.size(), ctx->Name());
    ASSERT_TRUE(rkey_or.ok());
    EXPECT_THAT(*rkey_or, Eq(mr->rkey));
  }
}

TEST_F(RdmaMemoryTest, SubSliceContainmentLookup) {
  RdmaMemory mem_manager(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& ctx : dev_->Contexts()) {
    struct ibv_mr* mr =
        mem_manager.GetMemoryRegion(buffer.data(), buffer.size(), ctx->Name());
    ASSERT_THAT(mr, NotNull());

    // Sub-slice inside the buffer (e.g. offset +512, length 128).
    const uint8_t* slice_addr = buffer.data() + 512;
    constexpr size_t kSliceLen = 128;

    auto slice_lkey_or =
        mem_manager.GetLKey(slice_addr, kSliceLen, ctx->Name());
    ASSERT_TRUE(slice_lkey_or.ok());
    EXPECT_THAT(*slice_lkey_or, Eq(mr->lkey));

    auto slice_rkey_or =
        mem_manager.GetRKey(slice_addr, kSliceLen, ctx->Name());
    ASSERT_TRUE(slice_rkey_or.ok());
    EXPECT_THAT(*slice_rkey_or, Eq(mr->rkey));
  }
}

TEST_F(RdmaMemoryTest, OutOfBoundsSliceLookupFails) {
  RdmaMemory mem_manager(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& ctx : dev_->Contexts()) {
    // Slice that starts inside buffer but exceeds capacity (e.g. offset +4000,
    // length 200).
    const uint8_t* oob_addr = buffer.data() + 4000;
    constexpr size_t kOobLen = 200;

    EXPECT_THAT(mem_manager.GetMemoryRegion(oob_addr, kOobLen, ctx->Name()),
                IsNull());
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetLKey(oob_addr, kOobLen, ctx->Name()).status()));
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetRKey(oob_addr, kOobLen, ctx->Name()).status()));
  }
}

TEST_F(RdmaMemoryTest, LookupOnNonExistentDeviceFails) {
  RdmaMemory mem_manager(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  EXPECT_THAT(mem_manager.GetMemoryRegion(buffer.data(), buffer.size(),
                                          "nonexistent_device"),
              IsNull());
  EXPECT_TRUE(absl::IsNotFound(
      mem_manager.GetLKey(buffer.data(), buffer.size(), "nonexistent_device")
          .status()));
  EXPECT_TRUE(absl::IsNotFound(
      mem_manager.GetRKey(buffer.data(), buffer.size(), "nonexistent_device")
          .status()));
}

TEST_F(RdmaMemoryTest, DuplicateOrContainedRegistrationFails) {
  RdmaMemory mem_manager(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  // Exact duplicate registration should fail.
  EXPECT_TRUE(absl::IsAlreadyExists(
      mem_manager.RegisterMemory(buffer.data(), buffer.size())));

  // Sub-range registration within existing registered slab should fail.
  EXPECT_TRUE(absl::IsAlreadyExists(
      mem_manager.RegisterMemory(buffer.data() + 100, 500)));
}

TEST_F(RdmaMemoryTest, UnregisterMemory) {
  RdmaMemory mem_manager(dev_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  // Unregister.
  ASSERT_TRUE(mem_manager.UnregisterMemory(buffer.data()).ok());

  // Subsequent lookups should fail.
  for (const auto& ctx : dev_->Contexts()) {
    EXPECT_THAT(
        mem_manager.GetMemoryRegion(buffer.data(), buffer.size(), ctx->Name()),
        IsNull());
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetLKey(buffer.data(), buffer.size(), ctx->Name())
            .status()));
  }

  // Second unregistration should fail with NotFound.
  EXPECT_TRUE(absl::IsNotFound(mem_manager.UnregisterMemory(buffer.data())));
}

TEST_F(RdmaMemoryTest, GetDefaultKeys) {
  RdmaMemory mem_manager(dev_.get());

  // Before registration, default keys should be 0.
  for (const auto& ctx : dev_->Contexts()) {
    EXPECT_EQ(mem_manager.GetDefaultLKey(ctx->Name()), 0);
    EXPECT_EQ(mem_manager.GetDefaultRKey(ctx->Name()), 0);
  }

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0);
  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  // After registration, default keys should match registered keys.
  for (const auto& ctx : dev_->Contexts()) {
    EXPECT_NE(mem_manager.GetDefaultLKey(ctx->Name()), 0);
    EXPECT_NE(mem_manager.GetDefaultRKey(ctx->Name()), 0);
    EXPECT_EQ(mem_manager.GetDefaultLKey(ctx->Name()),
              *mem_manager.GetLKey(buffer.data(), buffer.size(), ctx->Name()));
    EXPECT_EQ(mem_manager.GetDefaultRKey(ctx->Name()),
              *mem_manager.GetRKey(buffer.data(), buffer.size(), ctx->Name()));
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
