#include "src/util/util.h"

#include <sys/socket.h>

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/random/random.h"

namespace peregrine::util::testing {
namespace {

TEST(UtilTest, Random) {
  absl::BitGen gen;
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(Random(gen, 0, 0), 0);
    EXPECT_EQ(Random(gen, 1, 1), 1);
    const int n = Random(gen, 1, 10);
    EXPECT_LE(1, n);
    EXPECT_LE(n, 10);
  }
}

TEST(UtilTest, FindPort) {
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
