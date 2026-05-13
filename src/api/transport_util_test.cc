#include "src/api/transport_util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportUtilTest, Create) {
  EXPECT_THAT(CreateTransport(), ::testing::NotNull());
}

}  // namespace
}  // namespace peregrine::testing
