#include "src/internal/socket/socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_param.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

class SocketUtilTest : public TestWithParam<SocketTestParam> {
 protected:
  SocketUtilTest() : cfg_(GetParam()) {}

 protected:
  const SocketTestConfig cfg_;
};

INSTANTIATE_TEST_SUITE_P(, SocketUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(SocketUtilTest, Basic) {
  ASSERT_FALSE(IsValidSocket(fd_t(-1)));
  ASSERT_FALSE(IsValidSocket(fd_t(-2)));

    for (int type : {SOCK_STREAM, SOCK_DGRAM}) {
      const std::string proto = type == SOCK_STREAM ? "tcp" : "udp";
      const fd_t fd = CreateSocket(cfg_.family, type, cfg_.blocking);
      ASSERT_GE(fd.value(), 0);
      ASSERT_TRUE(IsValidSocket(fd));
      LOG(INFO) << SuccessMsg(proto, "created", fd);

      int on = 1, off = 0;
      EXPECT_TRUE(SetOption(fd, SO_REUSEADDR, &on, sizeof(on)));
      EXPECT_TRUE(SetOption(fd, SO_REUSEADDR, &off, sizeof(off)));

      EXPECT_TRUE(SetBlockingMode(fd));
      EXPECT_TRUE(IsBlockingMode(fd));
      EXPECT_FALSE(IsNonBlockingMode(fd));

      EXPECT_TRUE(SetNonBlockingMode(fd));
      EXPECT_TRUE(IsNonBlockingMode(fd));
      EXPECT_FALSE(IsBlockingMode(fd));

      if (cfg_.family == AF_INET) {
        EXPECT_EQ(SelfAddrPort(fd), "0.0.0.0:0");
      } else {
        EXPECT_EQ(SelfAddrPort(fd), "[::]:0");
      }
        EXPECT_EQ(PeerAddrPort(fd), "*");  // not connected

        LOG(INFO) << "ip:port pair = " << AddrPortPair(fd);
        LOG(INFO) << SuccessMsg(proto, "close", fd);

        ASSERT_TRUE(IsValidSocket(fd));
        ASSERT_EQ(::close(fd.value()), 0);
  }
}

TEST(SocketUtilBasicTest, ToIPv4AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in* sa_in = (struct sockaddr_in*)&ss;
  sa_in->sin_family = AF_INET;
  sa_in->sin_port = htons(23456);
  ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa_in->sin_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "127.0.0.1:23456");
}

TEST(SocketUtilBasicTest, ToIPv6AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in6* sa_in6 = (struct sockaddr_in6*)&ss;
  sa_in6->sin6_family = AF_INET6;
  sa_in6->sin6_port = htons(34567);
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa_in6->sin6_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "[::1]:34567");
}

}  // namespace
}  // namespace peregrine::internal::testing
