#include "src/internal/socket/psp/tcp_psp_helper.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace peregrine::internal {
namespace {

TEST(TcpPspHelperTest, OssStub) {
  EXPECT_FALSE(IsPspSupported());
}

}  // namespace
}  // namespace peregrine::internal
