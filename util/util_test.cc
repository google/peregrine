#include "util/util.h"

#include <sys/socket.h>

#include <cstdint>

#include "gtest/gtest.h"

namespace peregrine::util::testing {
namespace {

TEST(SocketTestUtilTest, FindPort) {
  for (const int family : {AF_INET, AF_INET6}) {
    for (const bool tcp : {true, false}) {
      const uint16_t port = FindFreePort(family, tcp);
      if (port > 0) {
        EXPECT_TRUE(10'000 <= port && port <= 65'535);
      }
    }
  }
}

}  // namespace
}  // namespace peregrine::util::testing
