#include "peregrine/src/internal/routing/route.h"

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal::testing {
namespace {

using util::IpAddr;

class RouteTest : public ::testing::Test {
 protected:
  RouteTest() : localhost_(IpAddr::Create("127.0.0.1").value()) {
    CHECK(localhost_.IsIPv4());
    CHECK(localhost_.IsLoopback());
    CHECK_EQ(route_.Size(), 0);
  }

  // First non-loopback IPv4 address on this host, or nullopt if it has none.
  std::optional<IpAddr> FirstNonLoopbackIpv4Addr() const {
    ifaddrs* ifa_list = nullptr;
    CHECK_EQ(getifaddrs(&ifa_list), 0);
    absl::Cleanup free_list = [ifa_list] { freeifaddrs(ifa_list); };
    for (const ifaddrs* ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
      const sockaddr* sa = ifa->ifa_addr;
      if (sa == nullptr || sa->sa_family != AF_INET) continue;
      const auto* sin = reinterpret_cast<const sockaddr_in*>(sa);
      if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;
      char ip[INET_ADDRSTRLEN] = {};
      CHECK_NE(inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)), nullptr);
      return IpAddr::Create(ip);
    }
    return std::nullopt;
  }

 protected:
  IpAddr localhost_;
  Route route_;
};

TEST_F(RouteTest, LoopbackSourceToLoopbackPeer) {
  EXPECT_EQ(route_.Size(), 0);

  const IpAddr src = IpAddr::Create("127.0.0.3").value();
  EXPECT_TRUE(route_.FindOrInsert(src, localhost_));
  EXPECT_EQ(route_.Size(), 1);
  // An identical call is served from the cache and adds no entry.
  EXPECT_TRUE(route_.FindOrInsert(src, localhost_));
  EXPECT_EQ(route_.Size(), 1);
}

TEST_F(RouteTest, UnownedSourceIsNotFound) {
  const IpAddr src = IpAddr::Create("169.254.1.1").value();
  EXPECT_FALSE(route_.FindOrInsert(src, localhost_));
  EXPECT_EQ(route_.Size(), 1);
  // Re-evaluated, not served from the cache.
  EXPECT_FALSE(route_.FindOrInsert(src, localhost_));
  EXPECT_EQ(route_.Size(), 1);
}

TEST_F(RouteTest, NonLoopbackSourceDoesNotRouteToLoopbackPeer) {
  const std::optional<IpAddr> ip = FirstNonLoopbackIpv4Addr();
  if (!ip.has_value()) {
    GTEST_SKIP() << "host has no non-loopback IPv4 address";
  }
  // Kernel egresses loopback peers via `lo`, not the NIC owning `ip`.
  EXPECT_FALSE(route_.FindOrInsert(*ip, localhost_));
}

TEST_F(RouteTest, InvalidPeerIsInvalidArgument) {
  EXPECT_FALSE(route_.FindOrInsert(localhost_, IpAddr()));
}

}  // namespace
}  // namespace peregrine::internal::testing
