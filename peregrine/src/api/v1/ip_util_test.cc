#include "peregrine/src/api/v1/ip_util.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_udp.h"
#include "peregrine/src/internal/util/test_util.h"

namespace peregrine::v1::testing {
namespace {

using internal::Endpoint;
using internal::UdpSocket;
using internal::testing::TestOnly_CreateUdpSocket;

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

}  // namespace
}  // namespace peregrine::v1::testing
