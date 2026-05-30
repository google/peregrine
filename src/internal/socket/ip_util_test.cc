#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>
#include <optional>
#include <string_view>

#include "gtest/gtest.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

TEST(IpUtilTest, ParseIPv4Addr) {
  const std::optional<ipv4_t> a = ParseIPv4Addr(kIPv4AnyAddr);
  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(ntohl(a.value().s_addr), 0);

  const std::optional<ipv4_t> b = ParseIPv4Addr(kIPv4Localhost);
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(ntohl(b.value().s_addr), 0x7f000001);

  EXPECT_FALSE(ParseIPv4Addr("?").has_value());
}

TEST(IpUtilTest, ParseIPv6Addr) {
  const std::optional<ipv6_t> a = ParseIPv6Addr(kIPv6AnyAddr);
  EXPECT_TRUE(a.has_value());
  const ipv6_t aa = a.value();
  EXPECT_EQ(std::memcmp(&aa, &in6addr_any, sizeof(aa)), 0);

  const std::optional<ipv6_t> b = ParseIPv6Addr(kIPv6Localhost);
  EXPECT_TRUE(b.has_value());
  const ipv6_t bb = b.value();
  EXPECT_EQ(std::memcmp(&bb, &in6addr_loopback, sizeof(bb)), 0);

  EXPECT_FALSE(ParseIPv6Addr("?").has_value());
}

TEST(IpUtilTest, ToIPv4AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in* sa_in = (struct sockaddr_in*)&ss;
  sa_in->sin_family = AF_INET;
  sa_in->sin_port = htons(12345);
  ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa_in->sin_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "127.0.0.1:12345");
}

TEST(IpUtilTest, ToIPv6AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in6* sa_in6 = (struct sockaddr_in6*)&ss;
  sa_in6->sin6_family = AF_INET6;
  sa_in6->sin6_port = htons(23456);
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa_in6->sin6_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "[::1]:23456");
}

TEST(IpUtilTest, BuildIPv4Sockaddr) {
  const std::optional<ipv4_t> ip4 = ParseIPv4Addr(kIPv4Localhost);
  const IpAddr ip = ip4.value();
  struct sockaddr_in sa = BuildIPv4Sockaddr(ip, 34567);
  EXPECT_EQ(sa.sin_family, AF_INET);
  EXPECT_EQ(sa.sin_port, htons(34567));
  EXPECT_EQ(sa.sin_addr.s_addr, inet_addr("127.0.0.1"));
}

TEST(IpUtilTest, BuildIPv6Sockaddr) {
  const std::optional<ipv6_t> ip6 = ParseIPv6Addr(kIPv6Localhost);
  const IpAddr ip = ip6.value();
  struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, 45678);
  EXPECT_EQ(sa.sin6_family, AF_INET6);
  EXPECT_EQ(sa.sin6_port, htons(45678));
  struct in6_addr addr2;
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &addr2), 1);
  EXPECT_EQ(std::memcmp(&sa.sin6_addr, &addr2, sizeof(addr2)), 0);
}

}  // namespace
}  // namespace peregrine::internal::testing
