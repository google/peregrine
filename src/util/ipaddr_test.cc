#include "src/util/ipaddr.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <optional>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::util::testing {
namespace {

TEST(IPv4AddrTest, ParseIPv4Addr) {
  const std::optional<ipv4_t> a = ParseIPv4Addr("0.0.0.0");
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(ntohl(a.value().s_addr), 0);

  const std::optional<ipv4_t> b = ParseIPv4Addr("127.0.0.1");
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(ntohl(b.value().s_addr), 0x7f000001);

  EXPECT_FALSE(ParseIPv4Addr("?").has_value());
}

TEST(IPv6AddrTest, ParseIPv6Addr) {
  const std::optional<ipv6_t> a = ParseIPv6Addr("::");
  EXPECT_TRUE(a.has_value());
  const ipv6_t aa = a.value();
  EXPECT_EQ(std::memcmp(&aa, &in6addr_any, sizeof(aa)), 0);

  const std::optional<ipv6_t> b = ParseIPv6Addr("::1");
  EXPECT_TRUE(b.has_value());
  const ipv6_t bb = b.value();
  EXPECT_EQ(std::memcmp(&bb, &in6addr_loopback, sizeof(bb)), 0);

  EXPECT_FALSE(ParseIPv6Addr("?").has_value());
}

TEST(IPv4AddrTest, ToString) {
  const ipv4_t a{.s_addr = htonl(INADDR_ANY)};
  EXPECT_EQ(ToIPv4String(a), "0.0.0.0");
  const IpAddr aa(a);
  EXPECT_TRUE(aa.IsZero());

  const ipv4_t b{.s_addr = htonl(INADDR_LOOPBACK)};
  EXPECT_EQ(ToIPv4String(b), "127.0.0.1");
  const IpAddr bb(b);
  EXPECT_FALSE(bb.IsZero());
}

TEST(IPv6AddrTest, ToString) {
  const ipv6_t a = IN6ADDR_ANY_INIT;
  EXPECT_EQ(ToIPv6String(a), "::");
  const IpAddr aa(a);
  EXPECT_TRUE(aa.IsZero());

  const ipv6_t b = IN6ADDR_LOOPBACK_INIT;
  EXPECT_EQ(ToIPv6String(b), "::1");
  const IpAddr bb(b);
  EXPECT_FALSE(bb.IsZero());
}

TEST(IpAddrTest, IPv4) {
  const ipv4_t v4_0{.s_addr = INADDR_ANY};
  const ipv4_t v4_1{.s_addr = htonl(INADDR_LOOPBACK)};
  const IpAddr a(v4_0);
  const IpAddr b(v4_1);
  const IpAddr a0 = a;
  EXPECT_TRUE(a.IsIPv4());
  EXPECT_TRUE(b.IsIPv4());
  EXPECT_EQ(a.AddressFamily(), AF_INET);
  EXPECT_EQ(b.AddressFamily(), AF_INET);
  EXPECT_EQ(a, a0);
  EXPECT_NE(a, b);
  EXPECT_EQ(a.Hash(), a0.Hash());
  LOG(INFO) << a;
  LOG(INFO) << b;
}

TEST(IpAddrTest, IPv6) {
  const ipv6_t v6_0 = IN6ADDR_ANY_INIT;
  const ipv6_t v6_1 = IN6ADDR_LOOPBACK_INIT;
  const IpAddr a(v6_0);
  const IpAddr b(v6_1);
  const IpAddr a0 = a;
  EXPECT_TRUE(a.IsIPv6());
  EXPECT_TRUE(b.IsIPv6());
  EXPECT_EQ(a.AddressFamily(), AF_INET6);
  EXPECT_EQ(b.AddressFamily(), AF_INET6);
  EXPECT_EQ(a, a0);
  EXPECT_NE(a, b);
  EXPECT_EQ(a.Hash(), a0.Hash());
  LOG(INFO) << a;
  LOG(INFO) << b;
}

}  // namespace
}  // namespace peregrine::util::testing
