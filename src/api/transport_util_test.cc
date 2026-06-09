#include "src/api/transport_util.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "src/util/util.h"

namespace peregrine::testing {
namespace {

TEST(TransportUtilTest, Create) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string ip = family == AF_INET ? "127.0.0.1" : "[::1]";
    const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
    const std::string endpoint = absl::StrCat(ip, ":", port);
    EXPECT_THAT(CreateTransport(endpoint), ::testing::NotNull());
  }
}

}  // namespace
}  // namespace peregrine::testing
