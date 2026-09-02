#include "src/api/transport_util.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "src/api/transport_types.h"
#include "src/util/util.h"

namespace peregrine::testing {
namespace {

using ::testing::IsNull;
using ::testing::NotNull;

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
  EXPECT_THAT(CreateTransport("127.0.0.1:0"), IsNull());
  EXPECT_THAT(CreateTransport("[::1]:0"), IsNull());
}

TEST(TransportUtilTest, TransportTypeSelection) {
  const uint16_t port_tcp = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string ep_tcp = absl::StrFormat("127.0.0.1:%d", port_tcp);
  EXPECT_THAT(CreateTransport(ep_tcp, TransportType::kTcp, kNumConnsPerPeer),
              NotNull());

  const uint16_t port_rdma = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string ep_rdma = absl::StrFormat("127.0.0.1:%d", port_rdma);
  EXPECT_THAT(CreateTransport(ep_rdma, TransportType::kRdma, kNumConnsPerPeer),
              IsNull());
}

TEST(TransportUtilTest, RequireDataplaneEncryption) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  const std::string ep = absl::StrFormat("127.0.0.1:%d", port);
  EXPECT_THAT(CreateTransport(ep, TransportType::kTcp, kNumConnsPerPeer,
                              /*require_dataplane_encryption=*/true),
              NotNull());
}

}  // namespace
}  // namespace peregrine::testing
