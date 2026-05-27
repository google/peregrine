#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>

#include <cstring>

#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace peregrine::testing {
namespace {

constexpr absl::string_view kIPv4AnyAddr = "0.0.0.0";
constexpr absl::string_view kIPv4Localhost = "127.0.0.1";
constexpr absl::string_view kIPv6AnyAddr = "::";
constexpr absl::string_view kIPv6Localhost = "::1";

TEST(IpUtilTest, AddressFamily) {
  EXPECT_EQ(AddressFamily(kIPv4AnyAddr), AF_INET);
  EXPECT_EQ(AddressFamily(kIPv4Localhost), AF_INET);
  EXPECT_EQ(AddressFamily(kIPv6AnyAddr), AF_INET6);
  EXPECT_EQ(AddressFamily(kIPv6Localhost), AF_INET6);
  EXPECT_EQ(AddressFamily("?"), AF_UNSPEC);
}

TEST(IpUtilTest, IsIPv4Addr) {
  EXPECT_TRUE(IsIPv4Addr(kIPv4AnyAddr));
  EXPECT_TRUE(IsIPv4Addr(kIPv4Localhost));
  EXPECT_FALSE(IsIPv4Addr(kIPv6AnyAddr));
  EXPECT_FALSE(IsIPv4Addr(kIPv6Localhost));
  EXPECT_FALSE(IsIPv4Addr("?"));
}

TEST(IpUtilTest, IsIPv6Addr) {
  EXPECT_TRUE(IsIPv6Addr(kIPv6AnyAddr));
  EXPECT_TRUE(IsIPv6Addr(kIPv6Localhost));
  EXPECT_FALSE(IsIPv6Addr(kIPv4AnyAddr));
  EXPECT_FALSE(IsIPv6Addr(kIPv4Localhost));
  EXPECT_FALSE(IsIPv6Addr("?"));
}

TEST(IpUtilTest, ParseIPv4Addr) {
  const struct in_addr a = ParseIPv4Addr(kIPv4AnyAddr);
  EXPECT_EQ(ntohl(a.s_addr), 0);

  const struct in_addr b = ParseIPv4Addr(kIPv4Localhost);
  EXPECT_EQ(ntohl(b.s_addr), 0x7f000001);

  const struct in_addr c = ParseIPv4Addr("?");
  EXPECT_EQ(ntohl(c.s_addr), 0);
}

TEST(IpUtilTest, ParseIPv6Addr) {
  const struct in6_addr a = ParseIPv6Addr(kIPv6AnyAddr);
  EXPECT_EQ(std::memcmp(&a, &in6addr_any, sizeof(a)), 0);

  const struct in6_addr b = ParseIPv6Addr(kIPv6Localhost);
  EXPECT_EQ(std::memcmp(&b, &in6addr_loopback, sizeof(b)), 0);

  const struct in6_addr c = ParseIPv6Addr("?");
  EXPECT_EQ(std::memcmp(&c, &in6addr_any, sizeof(c)), 0);
}

TEST(IpUtilTest, ToIPv4AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in* sa_in = (struct sockaddr_in*)&ss;
  sa_in->sin_family = AF_INET;
  sa_in->sin_port = htons(1234);
  ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa_in->sin_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "127.0.0.1:1234");
}

TEST(IpUtilTest, ToIPv6AddrPortString) {
  struct sockaddr_storage ss;
  struct sockaddr_in6* sa_in6 = (struct sockaddr_in6*)&ss;
  sa_in6->sin6_family = AF_INET6;
  sa_in6->sin6_port = htons(1234);
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa_in6->sin6_addr), 1);
  EXPECT_EQ(ToIpAddrPortString(ss), "[::1]:1234");
}

TEST(IpUtilTest, BuildIPv4Sockaddr) {
  struct sockaddr_in sa = BuildIPv4Sockaddr(kIPv4Localhost, 1234);
  EXPECT_EQ(sa.sin_family, AF_INET);
  EXPECT_EQ(sa.sin_port, htons(1234));
  EXPECT_EQ(sa.sin_addr.s_addr, inet_addr("127.0.0.1"));
}

TEST(IpUtilTest, BuildIPv6Sockaddr) {
  struct sockaddr_in6 sa = BuildIPv6Sockaddr(kIPv6Localhost, 1234);
  EXPECT_EQ(sa.sin6_family, AF_INET6);
  EXPECT_EQ(sa.sin6_port, htons(1234));
  struct in6_addr addr2;
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &addr2), 1);
  EXPECT_EQ(std::memcmp(&sa.sin6_addr, &addr2, sizeof(addr2)), 0);
}

}  // namespace
}  // namespace peregrine::testing
