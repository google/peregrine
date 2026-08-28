#include "src/internal/rdma/rdma_memory_manager.h"

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
#include "src/internal/rdma/rdma_device_manager.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

class RdmaMemoryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto dev_mgr_or = RdmaDeviceManager::Create();
    if (absl::IsNotFound(dev_mgr_or.status())) {
      GTEST_SKIP()
          << "No hardware RDMA devices available in this test environment.";
    }
    ASSERT_TRUE(dev_mgr_or.ok()) << dev_mgr_or.status();
    dev_mgr_ = std::move(*dev_mgr_or);
    ASSERT_THAT(dev_mgr_, NotNull());
    ASSERT_FALSE(dev_mgr_->Devices().empty());
  }

  std::unique_ptr<RdmaDeviceManager> dev_mgr_;
};

TEST_F(RdmaMemoryManagerTest, RegisterAndLookupBaseBuffer) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    struct ibv_mr* mr = mem_manager.GetMemoryRegion(
        buffer.data(), buffer.size(), dev_ctx->Name());
    ASSERT_THAT(mr, NotNull());
    EXPECT_THAT(mr->addr, Eq(buffer.data()));
    EXPECT_THAT(mr->length, Eq(buffer.size()));

    auto lkey_or =
        mem_manager.GetLKey(buffer.data(), buffer.size(), dev_ctx->Name());
    ASSERT_TRUE(lkey_or.ok());
    EXPECT_THAT(*lkey_or, Eq(mr->lkey));

    auto rkey_or =
        mem_manager.GetRKey(buffer.data(), buffer.size(), dev_ctx->Name());
    ASSERT_TRUE(rkey_or.ok());
    EXPECT_THAT(*rkey_or, Eq(mr->rkey));
  }
}

TEST_F(RdmaMemoryManagerTest, SubSliceContainmentLookup) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    struct ibv_mr* mr = mem_manager.GetMemoryRegion(
        buffer.data(), buffer.size(), dev_ctx->Name());
    ASSERT_THAT(mr, NotNull());

    // Sub-slice inside the buffer (e.g. offset +512, length 128).
    const uint8_t* slice_addr = buffer.data() + 512;
    constexpr size_t kSliceLen = 128;

    auto slice_lkey_or =
        mem_manager.GetLKey(slice_addr, kSliceLen, dev_ctx->Name());
    ASSERT_TRUE(slice_lkey_or.ok());
    EXPECT_THAT(*slice_lkey_or, Eq(mr->lkey));

    auto slice_rkey_or =
        mem_manager.GetRKey(slice_addr, kSliceLen, dev_ctx->Name());
    ASSERT_TRUE(slice_rkey_or.ok());
    EXPECT_THAT(*slice_rkey_or, Eq(mr->rkey));
  }
}

TEST_F(RdmaMemoryManagerTest, OutOfBoundsSliceLookupFails) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    // Slice that starts inside buffer but exceeds capacity (e.g. offset +4000,
    // length 200).
    const uint8_t* oob_addr = buffer.data() + 4000;
    constexpr size_t kOobLen = 200;

    EXPECT_THAT(mem_manager.GetMemoryRegion(oob_addr, kOobLen, dev_ctx->Name()),
                IsNull());
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetLKey(oob_addr, kOobLen, dev_ctx->Name()).status()));
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetRKey(oob_addr, kOobLen, dev_ctx->Name()).status()));
  }
}

TEST_F(RdmaMemoryManagerTest, LookupOnNonExistentDeviceFails) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

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

TEST_F(RdmaMemoryManagerTest, DuplicateOrContainedRegistrationFails) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

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

TEST_F(RdmaMemoryManagerTest, DeregisterMemory) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0x5A);

  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  // Deregister.
  ASSERT_TRUE(mem_manager.DeregisterMemory(buffer.data()).ok());

  // Subsequent lookups should fail.
  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    EXPECT_THAT(mem_manager.GetMemoryRegion(buffer.data(), buffer.size(),
                                            dev_ctx->Name()),
                IsNull());
    EXPECT_TRUE(absl::IsNotFound(
        mem_manager.GetLKey(buffer.data(), buffer.size(), dev_ctx->Name())
            .status()));
  }

  // Second deregistration should fail with NotFound.
  EXPECT_TRUE(absl::IsNotFound(mem_manager.DeregisterMemory(buffer.data())));
}

TEST_F(RdmaMemoryManagerTest, GetDefaultRKey) {
  RdmaMemoryManager mem_manager(dev_mgr_.get());

  // Before registration, default RKey should be 0.
  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    EXPECT_EQ(mem_manager.GetDefaultRKey(dev_ctx->Name()), 0);
  }

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0);
  ASSERT_TRUE(mem_manager.RegisterMemory(buffer.data(), buffer.size()).ok());

  // After registration, default RKey should match registered RKey.
  for (const auto& dev_ctx : dev_mgr_->Devices()) {
    EXPECT_NE(mem_manager.GetDefaultRKey(dev_ctx->Name()), 0);
    EXPECT_EQ(
        mem_manager.GetDefaultRKey(dev_ctx->Name()),
        *mem_manager.GetRKey(buffer.data(), buffer.size(), dev_ctx->Name()));
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
