#include "src/internal/base/types.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::testing {
namespace {

TEST(IPv4AddrTest, ToString) {
  const ipv4_t a{.s_addr = htonl(INADDR_ANY)};
  EXPECT_EQ(ToIPv4String(a), "0.0.0.0");

  const ipv4_t b{.s_addr = htonl(INADDR_LOOPBACK)};
  EXPECT_EQ(ToIPv4String(b), "127.0.0.1");
}

TEST(IPv6AddrTest, ToString) {
  const ipv6_t a = IN6ADDR_ANY_INIT;
  EXPECT_EQ(ToIPv6String(a), "::");

  const ipv6_t b = IN6ADDR_LOOPBACK_INIT;
  EXPECT_EQ(ToIPv6String(b), "::1");
}

TEST(IpAddrTest, Basic) {
  const ipv4_t v4_0{.s_addr = INADDR_ANY};
  const ipv4_t v4_1{.s_addr = htonl(INADDR_LOOPBACK)};

  const IpAddr a = v4_0;
  const IpAddr b = v4_1;
  EXPECT_TRUE(IsIPv4(a));
  EXPECT_TRUE(IsIPv4(b));
  EXPECT_EQ(AddressFamily(a), AF_INET);
  EXPECT_EQ(AddressFamily(b), AF_INET);
  LOG(INFO) << ToString(a);
  LOG(INFO) << ToString(b);

  const ipv6_t v6_0 = IN6ADDR_ANY_INIT;
  const ipv6_t v6_1 = IN6ADDR_LOOPBACK_INIT;
  const IpAddr c = v6_0;
  const IpAddr d = v6_1;
  EXPECT_TRUE(IsIPv6(c));
  EXPECT_TRUE(IsIPv6(d));
  EXPECT_EQ(AddressFamily(c), AF_INET6);
  EXPECT_EQ(AddressFamily(d), AF_INET6);
  LOG(INFO) << ToString(c);
  LOG(INFO) << ToString(d);
}

}  // namespace
}  // namespace peregrine::testing
