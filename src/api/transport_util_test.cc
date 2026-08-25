#include "src/api/transport_util.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "src/util/util.h"

namespace peregrine::testing {
namespace {

using ::testing::IsNull;
using ::testing::NotNull;

constexpr int kNumConnsPerPeer = 4;

TEST(TransportUtilTest, ValidEndpoints) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
    const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
    const std::string ep = absl::StrFormat("%s:%d", ip, port);
    EXPECT_THAT(CreateTransport(ep, kNumConnsPerPeer), NotNull());
  }
}

TEST(TransportUtilTest, InvalidEndpoints) {
  EXPECT_THAT(CreateTransport("", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("invalid", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("127.0.0.1", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("9.9.9.999:80", kNumConnsPerPeer), IsNull());
}

TEST(TransportUtilTest, WildcardAndZeroPort) {
  EXPECT_THAT(CreateTransport("0.0.0.0:9999", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("[::]:9999", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("127.0.0.1:0", kNumConnsPerPeer), IsNull());
  EXPECT_THAT(CreateTransport("[::1]:0", kNumConnsPerPeer), IsNull());
}

}  // namespace
}  // namespace peregrine::testing
