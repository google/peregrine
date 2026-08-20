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

constexpr int kNumConnsPerPeer = 4;

TEST(TransportUtilTest, Create) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
    const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
    const std::string ep = absl::StrFormat("%s:%d", ip, port);
    EXPECT_THAT(CreateTransport(ep, kNumConnsPerPeer), ::testing::NotNull());
  }
}

TEST(TransportUtilTest, CreateInvalidEndpoints) {
  EXPECT_THAT(CreateTransport("", kNumConnsPerPeer), ::testing::IsNull());
  EXPECT_THAT(CreateTransport("invalid", kNumConnsPerPeer),
              ::testing::IsNull());
  EXPECT_THAT(CreateTransport("127.0.0.1", kNumConnsPerPeer),
              ::testing::IsNull());
  EXPECT_THAT(CreateTransport("999.999.999.999:80", kNumConnsPerPeer),
              ::testing::IsNull());
}

}  // namespace
}  // namespace peregrine::testing
