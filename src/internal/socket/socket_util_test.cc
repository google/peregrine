#include "src/internal/socket/socket_util.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace peregrine::testing {
namespace {

TEST(SocketUtilTest, Basic) {
  for (int family : {AF_INET, AF_INET6}) {
    for (int type : {SOCK_DGRAM, SOCK_STREAM}) {
      for (bool nonblocking : {true, false}) {
        ASSERT_OK_AND_ASSIGN(const int fd,
                             CreateSocket(family, type, nonblocking));
        ASSERT_GE(fd, 0);

        int on = 1, off = 0;
        EXPECT_OK(SetOption(fd, SO_REUSEADDR, &on, sizeof(on)));
        EXPECT_OK(SetOption(fd, SO_REUSEADDR, &off, sizeof(off)));

        EXPECT_OK(SetBlockingMode(fd));
        EXPECT_TRUE(IsBlockingMode(fd));
        EXPECT_FALSE(IsNonBlockingMode(fd));

        EXPECT_OK(SetNonBlockingMode(fd));
        EXPECT_TRUE(IsNonBlockingMode(fd));
        EXPECT_FALSE(IsBlockingMode(fd));

        if (family == AF_INET) {
          EXPECT_EQ(SelfAddrPort(fd), "0.0.0.0:0");
        } else {
          EXPECT_EQ(SelfAddrPort(fd), "[::]:0");
        }
        EXPECT_EQ(PeerAddrPort(fd), "*");  // not connected

        LOG(INFO) << AddrPortPair(fd);
        ASSERT_EQ(close(fd), 0);
      }
    }
  }
}

}  // namespace
}  // namespace peregrine::testing
