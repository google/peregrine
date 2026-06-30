#include "src/api/transport_util.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "src/util/util.h"

namespace peregrine::testing {
namespace {

std::string GenEndpoint(int family) {
  const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
  const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
  return absl::StrCat(ip, ":", port);
}

int NumConnsPerPeer(int family) { return family == AF_INET ? 1 : 2; }

TEST(TransportUtilTest, Create) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string e = GenEndpoint(family);
    const int n = NumConnsPerPeer(family);
    EXPECT_THAT(CreateTransport(e, n), ::testing::NotNull());
  }
}

}  // namespace
}  // namespace peregrine::testing
