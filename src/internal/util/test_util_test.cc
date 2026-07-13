#include "src/internal/util/test_util.h"

#include <sys/socket.h>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"

namespace peregrine::internal::testing {
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

TEST(TestUtilTest, LocalEndpoint) {
  for (const int family : {AF_INET, AF_INET6}) {
    for (const bool tcp : {true, false}) {
      const Endpoint e = TestOnly_LocalEndpoint(family, tcp);
      EXPECT_TRUE(e.IsValid());
      LOG(INFO) << e;
    }
  }
}

TEST(TestUtilTest, LocalHostInfo) {
  for (const int family : {AF_INET, AF_INET6}) {
    for (const bool tcp : {true, false}) {
      const HostInfo h = TestOnly_LocalHostInfo(family, tcp);
      EXPECT_TRUE(h.IsValid());
      LOG(INFO) << h;
    }
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
