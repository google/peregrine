#include "src/internal/socket/socket_util.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

TEST(SocketUtilTest, Basic) {
  for (int family : {AF_INET, AF_INET6}) {
    for (int type : {SOCK_STREAM, SOCK_DGRAM}) {
      const std::string proto = type == SOCK_STREAM ? "tcp" : "udp";
      for (bool nonblocking : {true, false}) {
        const int fd = CreateSocket(family, type, nonblocking);
        ASSERT_GE(fd, 0);
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

        if (family == AF_INET) {
          EXPECT_EQ(SelfAddrPort(fd), "0.0.0.0:0");
        } else {
          EXPECT_EQ(SelfAddrPort(fd), "[::]:0");
        }
        EXPECT_EQ(PeerAddrPort(fd), "*");  // not connected

        LOG(INFO) << "ip:port pair = " << AddrPortPair(fd);
        LOG(INFO) << SuccessMsg(proto, "close", fd);
        ASSERT_EQ(close(fd), 0);
      }
    }
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
