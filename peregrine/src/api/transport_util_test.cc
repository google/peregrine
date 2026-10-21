#include "peregrine/src/api/transport_util.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/transport_registry.h"
#include "peregrine/src/util/util.h"

namespace peregrine::testing {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::StartsWith;

constexpr int kNumConnsPerPeer = 4;

TEST(TransportUtilTest, ValidEndpoints) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
    const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
    const std::string ep = absl::StrFormat("%s:%d", ip, port);
    EXPECT_THAT(CreateTransport(ep), NotNull());
    EXPECT_THAT(CreateTransport(ep, TransportType::kTcp, kNumConnsPerPeer),
                NotNull());
  }
}

TEST(TransportUtilTest, InvalidEndpoints) {
  EXPECT_THAT(CreateTransport(""), IsNull());
  EXPECT_THAT(CreateTransport("invalid"), IsNull());
  EXPECT_THAT(CreateTransport("127.0.0.1"), IsNull());
  EXPECT_THAT(CreateTransport("9.9.9.999:80"), IsNull());
}

TEST(TransportUtilTest, WildcardAndZeroPort) {
  EXPECT_THAT(CreateTransport("0.0.0.0:9999"), IsNull());
  EXPECT_THAT(CreateTransport("[::]:9999"), IsNull());
  EXPECT_THAT(CreateTransport("0.0.0.0:0"), IsNull());
  EXPECT_THAT(CreateTransport("[::]:0"), IsNull());
}

TEST(TransportUtilTest, ZeroPortIsAutoAssigned) {
  for (int family : {AF_INET, AF_INET6}) {
    const std::string_view ip = family == AF_INET ? "127.0.0.1" : "[::1]";
    auto t = CreateTransport(absl::StrFormat("%s:0", ip));
    ASSERT_THAT(t, NotNull());
    const std::string ep = t->GetControlEndpoint();
    EXPECT_THAT(ep, StartsWith(absl::StrFormat("%s:", ip)));
    EXPECT_NE(ep, absl::StrFormat("%s:0", ip));
  }
}

TEST(TransportUtilTest, NonLocalAddress) {
  EXPECT_THAT(CreateTransport("192.0.2.1:0"), IsNull());
}

TEST(TransportUtilTest, IsTransportSupported) {
  EXPECT_TRUE(IsTransportSupported(TransportType::kTcp));
  EXPECT_FALSE(IsTransportSupported(static_cast<TransportType>(0)));
  EXPECT_FALSE(IsTransportSupported(static_cast<TransportType>(99)));
}

TEST(TransportUtilTest, TransportTypeTcp) {
  const uint16_t port_tcp = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string ep_tcp = absl::StrFormat("127.0.0.1:%d", port_tcp);
  EXPECT_THAT(CreateTransport(ep_tcp, TransportType::kTcp, kNumConnsPerPeer),
              NotNull());
}

TEST(TransportUtilTest, TransportTypeRdma) {
  const uint16_t port_rdma = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string ep_rdma = absl::StrFormat("127.0.0.1:%d", port_rdma);
  if (IsTransportSupported(TransportType::kRdma)) {
    EXPECT_THAT(
        CreateTransport(ep_rdma, TransportType::kRdma, kNumConnsPerPeer),
        NotNull());
  } else {
    // In hermetic unit test environments without RDMA NICs, RDMA transport
    // creation fails gracefully and returns nullptr.
    EXPECT_THAT(
        CreateTransport(ep_rdma, TransportType::kRdma, kNumConnsPerPeer),
        IsNull());
  }
}

TEST(TransportUtilTest, RegisteredTransportIsUsed) {
  // An otherwise unused transport type, distinct from those tested above.
  const auto kRegisteredType = static_cast<TransportType>(77);
  EXPECT_FALSE(IsTransportSupported(kRegisteredType));

  // Static because the registry (and thus the factory) outlives this test.
  static int create_calls = 0;
  static internal::Config created_config;
  internal::TransportRegistration registration;
  registration.create =
      [](const internal::Config& config, const internal::Endpoint&,
         internal::SecurityCredentials) -> std::unique_ptr<Transport> {
    ++create_calls;
    created_config = config;
    return nullptr;
  };
  registration.is_supported = [] { return true; };
  ASSERT_THAT(
      internal::RegisterTransport(kRegisteredType, std::move(registration)),
      IsOk());

  EXPECT_TRUE(IsTransportSupported(kRegisteredType));
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  EXPECT_THAT(CreateTransport(absl::StrFormat("127.0.0.1:%d", port),
                              kRegisteredType, kNumConnsPerPeer),
              IsNull());
  EXPECT_EQ(create_calls, 1);
  EXPECT_EQ(created_config.transport_type, kRegisteredType);
  EXPECT_EQ(created_config.num_conns_per_peer, kNumConnsPerPeer);
}

TEST(TransportUtilTest, RegisteredTransportCanBeUnsupported) {
  const auto kRegisteredType = static_cast<TransportType>(78);
  internal::TransportRegistration registration;
  registration.create =
      [](const internal::Config&, const internal::Endpoint&,
         internal::SecurityCredentials) -> std::unique_ptr<Transport> {
    return nullptr;
  };
  registration.is_supported = [] { return false; };
  ASSERT_THAT(
      internal::RegisterTransport(kRegisteredType, std::move(registration)),
      IsOk());
  EXPECT_FALSE(IsTransportSupported(kRegisteredType));
}

}  // namespace
}  // namespace peregrine::testing
