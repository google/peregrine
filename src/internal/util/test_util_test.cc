#include "src/internal/util/test_util.h"

#include <sys/socket.h>

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

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

TEST(TestUtilTest, TcpSocket) {
  for (const int family : {AF_INET, AF_INET6}) {
    const auto socket = TestOnly_CreateTcpSocket(family);
    EXPECT_NE(socket, nullptr);
  }
}

TEST(TestUtilTest, UdpSocket) {
  for (const int family : {AF_INET, AF_INET6}) {
    const auto socket = TestOnly_CreateUdpSocket(family);
    EXPECT_NE(socket, nullptr);
  }
}

}  // namespace
}  // namespace peregrine::testing
