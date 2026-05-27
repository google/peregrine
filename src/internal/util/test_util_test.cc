#include "src/internal/util/test_util.h"

#include <sys/socket.h>

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TestUtilTest, EoF) {
  EXPECT_EQ(kEoF.iov_base, nullptr);
  EXPECT_EQ(kEoF.iov_len, 0);
}

TEST(TestUtilTest, TcpPort) {
  for (const int family : {AF_INET, AF_INET6}) {
    EXPECT_NE(TestOnly_FindFreeTcpPort(family), 0);
  }
}

TEST(TestUtilTest, UdpPort) {
  for (const int family : {AF_INET, AF_INET6}) {
    EXPECT_NE(TestOnly_FindFreeUdpPort(family), 0);
  }
}

}  // namespace
}  // namespace peregrine::testing
