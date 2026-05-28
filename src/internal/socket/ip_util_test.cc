#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_util.h"

namespace peregrine::testing {
namespace {

using ::absl::StatusCode::kInvalidArgument;
using ::testing::status::StatusIs;

TEST(IpUtilTest, ParseIPv4Addr) {
  ASSERT_OK_AND_ASSIGN(const ipv4_t a, ParseIPv4Addr(kIPv4AnyAddr));
  EXPECT_EQ(ntohl(a.s_addr), 0);

  ASSERT_OK_AND_ASSIGN(const ipv4_t b, ParseIPv4Addr(kIPv4Localhost));
  EXPECT_EQ(ntohl(b.s_addr), 0x7f000001);

  EXPECT_THAT(ParseIPv4Addr("?"), StatusIs(kInvalidArgument));
}

TEST(IpUtilTest, ParseIPv6Addr) {
  ASSERT_OK_AND_ASSIGN(const ipv6_t a, ParseIPv6Addr(kIPv6AnyAddr));
  EXPECT_EQ(std::memcmp(&a, &in6addr_any, sizeof(a)), 0);

  ASSERT_OK_AND_ASSIGN(const ipv6_t b, ParseIPv6Addr(kIPv6Localhost));
  EXPECT_EQ(std::memcmp(&b, &in6addr_loopback, sizeof(b)), 0);

  EXPECT_THAT(ParseIPv6Addr("?"), StatusIs(kInvalidArgument));
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
  ASSERT_OK_AND_ASSIGN(ipv4_t ip4, ParseIPv4Addr(kIPv4Localhost));
  const IpAddr ip = ip4;
  struct sockaddr_in sa = BuildIPv4Sockaddr(ip, 34567);
  EXPECT_EQ(sa.sin_family, AF_INET);
  EXPECT_EQ(sa.sin_port, htons(34567));
  EXPECT_EQ(sa.sin_addr.s_addr, inet_addr("127.0.0.1"));
}

TEST(IpUtilTest, BuildIPv6Sockaddr) {
  ASSERT_OK_AND_ASSIGN(ipv6_t ip6, ParseIPv6Addr(kIPv6Localhost));
  const IpAddr ip = ip6;
  struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, 45678);
  EXPECT_EQ(sa.sin6_family, AF_INET6);
  EXPECT_EQ(sa.sin6_port, htons(45678));
  struct in6_addr addr2;
  ASSERT_EQ(inet_pton(AF_INET6, "::1", &addr2), 1);
  EXPECT_EQ(std::memcmp(&sa.sin6_addr, &addr2, sizeof(addr2)), 0);
}

}  // namespace
}  // namespace peregrine::testing
