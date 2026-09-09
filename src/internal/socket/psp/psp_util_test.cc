#include "src/internal/socket/psp/psp_util.h"

#include "gtest/gtest.h"

namespace peregrine::internal::psp::testing {
namespace {

TEST(TcpPspHelperTest, OssStub) {
  EXPECT_FALSE(IsPspSupported());
}

}  // namespace
}  // namespace peregrine::internal::psp::testing
