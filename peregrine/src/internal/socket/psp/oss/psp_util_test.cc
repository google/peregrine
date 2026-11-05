#include "peregrine/src/internal/socket/psp/psp_util.h"

#include "gtest/gtest.h"

namespace peregrine::internal::psp::testing {
namespace {

TEST(PspUtilTest, OssStub) {
  EXPECT_FALSE(IsPspSupported());
  EXPECT_EQ(GetPspTcpSyscalls(), nullptr);
}

}  // namespace
}  // namespace peregrine::internal::psp::testing
