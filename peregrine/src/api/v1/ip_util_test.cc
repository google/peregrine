#include "peregrine/src/api/v1/ip_util.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_udp.h"
#include "peregrine/src/internal/util/test_util.h"

namespace peregrine::v1::testing {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using internal::Endpoint;
using internal::UdpSocket;
using internal::testing::TestOnly_CreateUdpSocket;
using ::testing::HasSubstr;

constexpr std::string_view kPeer = "127.0.0.1:1111";

std::unique_ptr<UdpSocket> CreateUdpSocket() {
  auto socket = TestOnly_CreateUdpSocket(AF_INET, /*blocking=*/true);
  CHECK_NE(socket, nullptr);
  CHECK(!socket->Bind(Endpoint::Create("127.0.0.1:0")));
  CHECK(!socket->Connect(Endpoint::Create(kPeer)));
  return socket;
}

TEST(IpUtilTest, ValidFd) {
  const std::unique_ptr<UdpSocket> socket = CreateUdpSocket();
  const int fd = socket->fd().value();
  const std::string self = GetLocalEndpoint(fd);
  EXPECT_TRUE(absl::StartsWith(self, "127.0.0.1:"));
  EXPECT_NE(self, "127.0.0.1:0");
  EXPECT_EQ(GetAddrPortPair(fd), absl::StrCat(self, "--", kPeer));
}

TEST(IpUtilTest, InvalidFd) {
  EXPECT_EQ(GetLocalEndpoint(-1), "?");
  EXPECT_EQ(GetAddrPortPair(-1), "?--?");
}

// --- SourceIpRoutesToPeer tests ---

// First non-loopback IPv4 address on this host, or nullopt if it has none.
std::optional<std::string> FirstNonLoopbackIpv4Address() {
  ifaddrs* ifa_list = nullptr;
  CHECK_EQ(getifaddrs(&ifa_list), 0);
  absl::Cleanup free_list = [ifa_list] { freeifaddrs(ifa_list); };
  for (const ifaddrs* ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
    if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;
    char ip[INET_ADDRSTRLEN] = {};
    CHECK_NE(inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)), nullptr);
    return std::string(ip);
  }
  return std::nullopt;
}

TEST(SourceIpRoutesToPeerTest, LoopbackSourceToLoopbackPeer) {
  TestOnly_ClearRouteCache();
  EXPECT_THAT(SourceIpRoutesToPeer("127.0.0.3", "127.0.0.1:1"),
              IsOkAndHolds(true));
}

TEST(SourceIpRoutesToPeerTest, UnownedSourceIsNotFound) {
  TestOnly_ClearRouteCache();
  // TEST-NET-1 is never assigned to a local interface.
  EXPECT_THAT(SourceIpRoutesToPeer("192.0.2.1", "127.0.0.1:1"),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr("192.0.2.1")));
}

TEST(SourceIpRoutesToPeerTest, NonLoopbackSourceDoesNotRouteToLoopbackPeer) {
  TestOnly_ClearRouteCache();
  const std::optional<std::string> nic_ip = FirstNonLoopbackIpv4Address();
  if (!nic_ip.has_value()) {
    GTEST_SKIP() << "host has no non-loopback IPv4 address";
  }
  // The kernel egresses loopback peers via `lo`, not the NIC owning `nic_ip`.
  EXPECT_THAT(SourceIpRoutesToPeer(*nic_ip, "127.0.0.1:1"),
              IsOkAndHolds(false));
}

TEST(SourceIpRoutesToPeerTest, InvalidPeerIsInvalidArgument) {
  TestOnly_ClearRouteCache();
  EXPECT_THAT(SourceIpRoutesToPeer("127.0.0.1", "nocolon"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(SourceIpRoutesToPeerTest, SuccessfulResultsAreMemoizedPerSourceAndPeer) {
  TestOnly_ClearRouteCache();
  EXPECT_EQ(TestOnly_RouteCacheSize(), 0);

  EXPECT_THAT(SourceIpRoutesToPeer("127.0.0.3", "127.0.0.1:1"),
              IsOkAndHolds(true));
  EXPECT_EQ(TestOnly_RouteCacheSize(), 1);

  // An identical call is served from the cache and adds no entry.
  EXPECT_THAT(SourceIpRoutesToPeer("127.0.0.3", "127.0.0.1:1"),
              IsOkAndHolds(true));
  EXPECT_EQ(TestOnly_RouteCacheSize(), 1);

  // A different peer port is a distinct key.
  EXPECT_THAT(SourceIpRoutesToPeer("127.0.0.3", "127.0.0.1:2"),
              IsOkAndHolds(true));
  EXPECT_EQ(TestOnly_RouteCacheSize(), 2);
}

TEST(SourceIpRoutesToPeerTest, ErrorsAreNotMemoized) {
  TestOnly_ClearRouteCache();
  // TEST-NET-1 is never assigned to a local interface, so this fails uncached.
  EXPECT_THAT(SourceIpRoutesToPeer("192.0.2.1", "127.0.0.1:1"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_EQ(TestOnly_RouteCacheSize(), 0);

  // Re-evaluated, not served from the cache: still the same error.
  EXPECT_THAT(SourceIpRoutesToPeer("192.0.2.1", "127.0.0.1:1"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_EQ(TestOnly_RouteCacheSize(), 0);
}

TEST(SourceIpRoutesToPeerTest, HostnamePeerStillResolves) {
  TestOnly_ClearRouteCache();
  // The numeric-only fast path must fall back to name resolution. The value
  // is not asserted: it is true if `localhost` resolves to 127.0.0.1 first
  // and false if it resolves only to ::1 (no IPv4 address for the peer).
  ABSL_EXPECT_OK(SourceIpRoutesToPeer("127.0.0.3", "localhost:1"));
}

}  // namespace
}  // namespace peregrine::v1::testing
