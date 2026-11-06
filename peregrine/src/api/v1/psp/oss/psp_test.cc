#include "peregrine/src/api/v1/psp/psp.h"

#include "gtest/gtest.h"

namespace peregrine::v1::testing {
namespace {

TEST(PspOssTest, OssStub) { EXPECT_FALSE(IsPspSupported()); }

}  // namespace
}  // namespace peregrine::v1::testing
