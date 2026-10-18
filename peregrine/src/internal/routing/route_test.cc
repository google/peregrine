#include "peregrine/src/internal/routing/route.h"

#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstddef>
#include <optional>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal::testing {
namespace {

using util::IpAddr;

// The following TEST-NET address ranges are used in tests below.
// RFC 5737: 192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24
// RFC 3849: 2001:db8::/32

IpAddr Ip(const std::string_view s) {
  const std::optional<IpAddr> ip = IpAddr::Create(s);
  CHECK(ip.has_value()) << s;
  return *ip;
}

class RouteTest : public ::testing::Test {
 protected:
  RouteTest() : lo4_(Ip("127.0.0.1")), lo6_(Ip("::1")) {
    CHECK(lo4_.IsIPv4() && lo4_.IsLoopback());
    CHECK(lo6_.IsIPv6() && lo6_.IsLoopback());
  }

  const IpAddr lo4_;
  const IpAddr lo6_;
  Route route_;
};

// --- Loopback --------------------------------------------------------------

TEST_F(RouteTest, LoopbackV4ToLoopbackV4) {
  EXPECT_EQ(route_.Size(), 0);
  const IpAddr src = Ip("127.0.0.3");
  EXPECT_TRUE(route_.Check(src, lo4_));
  EXPECT_EQ(route_.Size(), 1);

  // An identical call is served from the cache and adds no entry.
  EXPECT_TRUE(route_.Check(src, lo4_));
  EXPECT_EQ(route_.Size(), 1);
}

TEST_F(RouteTest, LoopbackV6ToLoopbackV6) {
  EXPECT_TRUE(route_.Check(lo6_, lo6_));
  EXPECT_EQ(route_.Size(), 1);

  // IPv4-mapped loopback counts as loopback too.
  EXPECT_TRUE(route_.Check(Ip("::ffff:127.0.0.1"), lo6_));
  EXPECT_EQ(route_.Size(), 2);
}

TEST_F(RouteTest, LoopbackSourceToNonLoopbackPeerIsFalse) {
  EXPECT_FALSE(route_.Check(lo4_, IpAddr()));  // 0.0.0.0
  EXPECT_FALSE(route_.Check(lo4_, Ip("192.0.2.1")));
  EXPECT_FALSE(route_.Check(lo6_, Ip("::")));
  EXPECT_FALSE(route_.Check(lo6_, Ip("2001:db8::1")));
  // These are definitive answers, so they are cached.
  EXPECT_EQ(route_.Size(), 4);
}

TEST_F(RouteTest, NonLoopbackSourceToLoopbackPeerIsFalse) {
  // Decided without consulting the host: a non-loopback source never egresses
  // via `lo`, whether or not the host owns the source address.
  EXPECT_FALSE(route_.Check(Ip("192.0.2.1"), lo4_));
  EXPECT_FALSE(route_.Check(Ip("2001:db8::1"), lo6_));
  EXPECT_EQ(route_.Size(), 2);
}

// --- Mixed address families -----------------------------------------------

TEST_F(RouteTest, MixedFamilyIsFalse) {
  EXPECT_FALSE(route_.Check(lo4_, lo6_));
  EXPECT_FALSE(route_.Check(lo6_, lo4_));
  EXPECT_FALSE(route_.Check(Ip("192.0.2.1"), lo6_));
  EXPECT_FALSE(route_.Check(Ip("2001:db8::1"), lo4_));
  EXPECT_FALSE(route_.Check(lo4_, Ip("2001:db8::1")));
  EXPECT_FALSE(route_.Check(lo6_, Ip("192.0.2.1")));
  EXPECT_EQ(route_.Size(), 6);
}

// --- Cache semantics -------------------------------------------------------

TEST_F(RouteTest, UnownedSourceIsNotCached) {
  // TEST-NET-1 / 2001:db8::/32 are never assigned to a local interface, so
  // FindNic() fails. Failures are reported as false but not memoized, so the
  // next call re-evaluates instead of being served from the cache.
  EXPECT_FALSE(route_.Check(Ip("192.0.2.1"), Ip("192.0.2.2")));
  EXPECT_EQ(route_.Size(), 0);
  EXPECT_FALSE(route_.Check(Ip("192.0.2.1"), Ip("192.0.2.2")));
  EXPECT_EQ(route_.Size(), 0);
  EXPECT_FALSE(route_.Check(Ip("2001:db8::1"), Ip("2001:db8::2")));
  EXPECT_EQ(route_.Size(), 0);
}

TEST_F(RouteTest, CacheIsKeyedByOrderedPair) {
  const IpAddr a = Ip("127.0.0.3");
  EXPECT_TRUE(route_.Check(a, lo4_));
  EXPECT_TRUE(route_.Check(lo4_, a));
  EXPECT_EQ(route_.Size(), 2);
  EXPECT_TRUE(route_.Check(lo4_, lo4_));
  EXPECT_EQ(route_.Size(), 3);
}

TEST_F(RouteTest, ClearResetsCache) {
  EXPECT_TRUE(route_.Check(lo4_, lo4_));
  EXPECT_TRUE(route_.Check(lo6_, lo6_));
  EXPECT_FALSE(route_.Check(lo4_, lo6_));
  EXPECT_EQ(route_.Size(), 3);

  route_.Clear();
  EXPECT_EQ(route_.Size(), 0);

  // Same answers after re-evaluation.
  EXPECT_TRUE(route_.Check(lo4_, lo4_));
  EXPECT_EQ(route_.Size(), 1);
  EXPECT_FALSE(route_.Check(lo4_, lo6_));
  EXPECT_EQ(route_.Size(), 2);

  // Clearing an empty cache is a no-op.
  route_.Clear();
  route_.Clear();
  EXPECT_EQ(route_.Size(), 0);
}

// --- Host-dependent (skipped on hosts without a suitable NIC) -------------

class RouteHostTest : public RouteTest,
                      public ::testing::WithParamInterface<int> {
 protected:
  RouteHostTest() : RouteTest(), family_(GetParam()) {}
  const IpAddr& Loopback() const { return family_ == AF_INET ? lo4_ : lo6_; }
  const char* ExternalPeer() const {
    return family_ == AF_INET ? "192.0.2.1" : "2001:db8::1";
  }

 protected:
  const int family_;
};

TEST_P(RouteHostTest, NonLoopbackSourceToLoopbackPeer) {
  const auto ip = util::TestOnly_PickOneNonLoopbackIpAddr(family_);
  if (!ip.has_value()) {
    GTEST_SKIP() << "host has no suitable nic address";
  }
  // Kernel egresses loopback peers via `lo`, not the NIC owning `ip`.
  EXPECT_FALSE(route_.Check(*ip, Loopback()));
  EXPECT_EQ(route_.Size(), 1);
}

TEST_P(RouteHostTest, NonLoopbackSourceToExternalPeerIsStable) {
  // The answer depends on the host's routing table (true on a single-NIC
  // host, possibly false on a multi-NIC one), so only its stability is
  // asserted: two calls agree, and a definitive answer is cached exactly once.
  const auto ip = util::TestOnly_PickOneNonLoopbackIpAddr(family_);
  if (!ip.has_value()) {
    GTEST_SKIP() << "host has no suitable nic address";
  }
  const IpAddr peer = Ip(ExternalPeer());
  const bool first = route_.Check(*ip, peer);
  const size_t size = route_.Size();
  EXPECT_LE(size, 1);
  EXPECT_EQ(route_.Check(*ip, peer), first);
  EXPECT_EQ(route_.Size(), size);
}

INSTANTIATE_TEST_SUITE_P(, RouteHostTest, ::testing::Values(AF_INET, AF_INET6),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == AF_INET ? "IPv4" : "IPv6";
                         });

}  // namespace
}  // namespace peregrine::internal::testing
