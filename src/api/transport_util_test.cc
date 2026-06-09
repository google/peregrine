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
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string endpoint = absl::StrCat("127.0.0.1:", port);
  EXPECT_THAT(CreateTransport(endpoint), ::testing::NotNull());
}

}  // namespace
}  // namespace peregrine::testing
