#include "peregrine/src/util/ipaddr.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::util::testing {
namespace {

TEST(IpAddrTest, DefaultConstructedIsIPv4Zero) {
  const IpAddr a;
  EXPECT_TRUE(a.IsIPv4());
  EXPECT_FALSE(a.IsIPv6());
  EXPECT_TRUE(a.IsZero());
  EXPECT_FALSE(a.IsLoopback());
  EXPECT_FALSE(a.IsRoutable());
  EXPECT_EQ(a, IpAddr::Create("0.0.0.0"));
}

TEST(IpAddrTest, EqualityAcrossFamilies) {
  const IpAddr v4 = *IpAddr::Create("127.0.0.1");
  const IpAddr v6 = *IpAddr::Create("::1");
  const IpAddr mapped = *IpAddr::Create("::ffff:127.0.0.1");
  const IpAddr zero4 = *IpAddr::Create("0.0.0.0");
  const IpAddr zero6 = *IpAddr::Create("::");

  EXPECT_NE(v4, v6);
  EXPECT_NE(v4, mapped);
  EXPECT_NE(v6, mapped);
  EXPECT_NE(zero4, zero6);
  EXPECT_EQ(v4, *IpAddr::Create("127.0.0.1"));
  EXPECT_EQ(mapped, *IpAddr::Create("::ffff:7f00:1"));
}

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

TEST(IpAddrTest, IsLoopback) {
  // IPv4 loopback (127.0.0.0/8).
  EXPECT_TRUE(IpAddr::Create("127.0.0.1")->IsLoopback());
  EXPECT_TRUE(IpAddr::Create("127.1.2.3")->IsLoopback());
  EXPECT_TRUE(IpAddr::Create("127.255.255.255")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("128.0.0.1")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("126.255.255.255")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("10.0.0.1")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("0.0.0.0")->IsLoopback());

  // IPv6 loopback (::1).
  EXPECT_TRUE(IpAddr::Create("::1")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("::")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("2002::")->IsLoopback());

  // IPv4-mapped IPv6 loopback (::ffff:127.0.0.0/8).
  EXPECT_TRUE(IpAddr::Create("::ffff:127.0.0.1")->IsLoopback());
  EXPECT_FALSE(IpAddr::Create("::ffff:10.0.0.1")->IsLoopback());
}

TEST(IpAddrTest, CreateFromSockaddr) {
  const sockaddr* sa;

  const sockaddr_in v4 = {
      .sin_family = AF_INET,
      .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)},
  };
  sa = reinterpret_cast<const sockaddr*>(&v4);
  const std::optional<IpAddr> a4 = IpAddr::Create(*sa);
  EXPECT_TRUE(a4.has_value());
  EXPECT_TRUE(a4->IsIPv4());
  EXPECT_EQ(a4->RawAddrAndLen(),
            std::make_pair(&a4->IPv4Addr().s_addr, sizeof(ipv4_t)));
  EXPECT_EQ(*a4, IpAddr::Create("127.0.0.1"));

  const sockaddr_in6 v6 = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };
  sa = reinterpret_cast<const sockaddr*>(&v6);
  const std::optional<IpAddr> a6 = IpAddr::Create(*sa);
  EXPECT_TRUE(a6.has_value());
  EXPECT_TRUE(a6->IsIPv6());
  EXPECT_EQ(a6->RawAddrAndLen(),
            std::make_pair(a6->IPv6Addr().s6_addr, sizeof(ipv6_t)));
  EXPECT_EQ(*a6, IpAddr::Create("::1"));

  // Unsupported families (e.g. AF_PACKET entries from getifaddrs()).
  const sockaddr unspec = {.sa_family = AF_UNSPEC};
  EXPECT_FALSE(IpAddr::Create(unspec).has_value());
  const sockaddr packet = {.sa_family = AF_PACKET};
  EXPECT_FALSE(IpAddr::Create(packet).has_value());
}

TEST(IpAddrTest, IsRoutableIPv4) {
  // Routable: loopback and private ranges.
  for (const char* s :
       {"127.0.0.1", "10.0.0.1", "172.16.0.1", "192.168.1.1", "8.8.8.8",
        "1.0.0.1", "223.255.255.255", "169.253.255.255", "169.255.0.0"}) {
    EXPECT_TRUE(IpAddr::Create(s)->IsRoutable()) << s;
  }
  // Non-routable: unspecified, multicast and link-local.
  for (const char* s : {"0.0.0.0", "224.0.0.1", "239.255.255.255",
                        "169.254.0.1", "169.254.255.255"}) {
    EXPECT_FALSE(IpAddr::Create(s)->IsRoutable()) << s;
  }
}

TEST(IpAddrTest, IsRoutableIPv6) {
  // Routable: loopback, ULA (fc00::/7) and IPv4-mapped, etc.
  for (const char* s : {"::1", "2001:db8::1", "2600::1", "fd00::1", "fc00::1",
                        "::ffff:10.0.0.1", "fe7f::1"}) {
    EXPECT_TRUE(IpAddr::Create(s)->IsRoutable()) << s;
  }
  // Non-routable: unspecified, multicast, link-local and site-local.
  for (const char* s : {"::", "ff02::1", "ff0e::1", "fe80::1", "febf::1",
                        "fec0::1", "feff::1"}) {
    EXPECT_FALSE(IpAddr::Create(s)->IsRoutable()) << s;
  }
}

}  // namespace
}  // namespace peregrine::util::testing
