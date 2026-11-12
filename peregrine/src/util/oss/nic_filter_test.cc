#include "peregrine/src/util/nic_filter.h"

#include "gtest/gtest.h"

namespace peregrine::util::testing {
namespace {

TEST(NicFilterTest, OssDoesNotExclude) {
  EXPECT_FALSE(IsPlatformExcludedInterface("eth0"));
  EXPECT_FALSE(IsPlatformExcludedInterface("ula0"));
  EXPECT_FALSE(IsPlatformExcludedInterface("idpf_dontuse0"));
}

}  // namespace
}  // namespace peregrine::util::testing
