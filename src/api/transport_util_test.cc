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

std::string GenHostInfo(int family) {
  const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
  const uint16_t port1 = util::FindFreePort(family, /*tcp=*/true);
  const uint16_t port2 = util::FindFreePort(family, /*tcp=*/true);
  return absl::StrFormat("%s:%d, %s:%d", ip, port1, ip, port2);
}

int NumConnsPerPeer(int family) { return family == AF_INET ? 1 : 2; }

TEST(TransportUtilTest, Create) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string h = GenHostInfo(family);
    const int n = NumConnsPerPeer(family);
    EXPECT_THAT(CreateTransport(h, n), ::testing::NotNull());
  }
}

}  // namespace
}  // namespace peregrine::testing
