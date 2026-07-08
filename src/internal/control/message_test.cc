#include "src/internal/control/message.h"

#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal::testing {
namespace {

constexpr uint64_t kLaddr = 0x1000;
constexpr uint64_t kRaddr = 0x2000;
constexpr uint64_t kLen = 300;

TEST(MessageTest, RequestOp) {
  EXPECT_EQ(static_cast<int>(proto::Request::INVALID), 0);
  EXPECT_EQ(static_cast<int>(proto::Request::READ),
            static_cast<int>(Op::kRead));
  EXPECT_EQ(static_cast<int>(proto::Request::WRITE),
            static_cast<int>(Op::kWrite));
}

TEST(MessageTest, Serialization) {
  const Endpoint c = Endpoint::Create("0.0.0.0:10000");
  const Endpoint d0 = Endpoint::Create("10.0.0.1:35247");
  const Endpoint d1 = Endpoint::Create("10.0.0.2:51691");
  const HostInfo host = {.control_plane_listener = c,
                         .data_plane_listeners = {d0, d1}};
  ASSERT_TRUE(host.IsValid());
  const Request req = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(kLaddr),
      .raddr = reinterpret_cast<Byte*>(kRaddr),
      .len = kLen,
  };
  proto::ReqMsg a;
  Message::Convert(host, {req}, a);

  const std::string s = Message::Serialize(a);
  ASSERT_LE(s.size(), Message::kMaxLen);

  proto::ReqMsg b;
  ASSERT_FALSE(b.has_peer_requests());
  EXPECT_TRUE(Message::Deserialize(s, b));
  EXPECT_TRUE(b.has_peer_requests());

  const auto [hb, requests] = Message::Convert(b);
  EXPECT_EQ(hb.control_plane_listener, c);
  EXPECT_EQ(hb.data_plane_listeners.size(), 2);
  EXPECT_EQ(hb.data_plane_listeners[0], d0);
  EXPECT_EQ(hb.data_plane_listeners[1], d1);
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0], req);
}

}  // namespace
}  // namespace peregrine::internal::testing
