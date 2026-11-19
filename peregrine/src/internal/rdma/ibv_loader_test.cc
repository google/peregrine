#include "peregrine/src/internal/rdma/ibv_loader.h"

#include <infiniband/verbs.h>

#include <cerrno>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace peregrine::internal::ibv {
namespace {

using ::testing::HasSubstr;
using ::testing::NotNull;

TEST(IbvLoaderTest, EnsureLoadedIsIdempotentAndMatchesIsAvailable) {
  const absl::Status first = EnsureLoaded();
  const absl::Status second = EnsureLoaded();
  EXPECT_EQ(first.code(), second.code());
  EXPECT_EQ(IsAvailable(), first.ok());
}

TEST(IbvLoaderTest, UnavailableStatusExplainsHowToFix) {
  const absl::Status status = EnsureLoaded();
  if (status.ok()) GTEST_SKIP() << "libibverbs is available on this host";
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
  EXPECT_THAT(status.message(), HasSubstr("RDMA"));
  EXPECT_THAT(status.message(), HasSubstr("--define=peregrine_rdma=true"));
}

TEST(IbvLoaderTest, WrappersFailSoftlyWhenUnavailable) {
  if (IsAvailable()) GTEST_SKIP() << "libibverbs is available on this host";
  int num_devices = -1;
  errno = 0;
  EXPECT_EQ(GetDeviceList(&num_devices), nullptr);
  EXPECT_EQ(num_devices, 0);
  EXPECT_EQ(errno, ENOSYS);
  EXPECT_EQ(CloseDevice(nullptr), ENOSYS);
  EXPECT_EQ(DestroyQp(nullptr), ENOSYS);
  EXPECT_THAT(WcStatusStr(IBV_WC_SUCCESS), NotNull());
}

TEST(IbvLoaderTest, DeviceEnumerationWorksWhenAvailable) {
  if (!IsAvailable()) GTEST_SKIP() << "libibverbs is not available";
  int num_devices = -1;
  struct ibv_device** list = GetDeviceList(&num_devices);
  // No RDMA hardware is fine; the call itself must go through.
  EXPECT_GE(num_devices, 0);
  if (list != nullptr) FreeDeviceList(list);
  EXPECT_STREQ(WcStatusStr(IBV_WC_SUCCESS), "success");
}

}  // namespace
}  // namespace peregrine::internal::ibv
