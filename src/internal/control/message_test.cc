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
  const Endpoint c = Endpoint::Create("10.0.0.1:10000");
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

TEST(MessageTest, HostInfoExchange) {
  // Construct a valid Multi-NIC HostInfo configuration.
  const Endpoint c = Endpoint::Create("10.0.0.1:10000");
  const Endpoint d0 = Endpoint::Create("10.0.0.1:35247");
  const Endpoint d1 = Endpoint::Create("10.0.0.2:51691");
  const HostInfo source = {
      .control_plane_listener = c,
      .data_plane_listeners = {d0, d1},
  };
  ASSERT_TRUE(source.IsValid());

  // 1. ReqMsg End-to-End Round-Trip Validation
  proto::ReqMsg req_proto;
  ASSERT_TRUE(Message::Convert(source, req_proto));
  EXPECT_TRUE(req_proto.has_host_info());

  HostInfo dest_req;
  ASSERT_TRUE(Message::Convert(req_proto, dest_req));
  EXPECT_EQ(dest_req.control_plane_listener, c);
  ASSERT_EQ(dest_req.data_plane_listeners.size(), 2);
  EXPECT_EQ(dest_req.data_plane_listeners[0], d0);
  EXPECT_EQ(dest_req.data_plane_listeners[1], d1);

  // 2. RespMsg End-to-End Round-Trip Validation
  proto::RespMsg resp_proto;
  ASSERT_TRUE(Message::Convert(source, resp_proto));
  EXPECT_TRUE(resp_proto.has_host_info());

  HostInfo dest_resp;
  ASSERT_TRUE(Message::Convert(resp_proto, dest_resp));
  EXPECT_EQ(dest_resp.control_plane_listener, c);
  ASSERT_EQ(dest_resp.data_plane_listeners.size(), 2);
  EXPECT_EQ(dest_resp.data_plane_listeners[0], d0);
  EXPECT_EQ(dest_resp.data_plane_listeners[1], d1);

  // 3. Robustness/Negative Constraints: De-serialization of Empty Envelopes
  proto::ReqMsg empty_req;
  HostInfo empty_dest;
  EXPECT_FALSE(Message::Convert(empty_req, empty_dest));

  proto::RespMsg empty_resp;
  EXPECT_FALSE(Message::Convert(empty_resp, empty_dest));
}

}  // namespace
}  // namespace peregrine::internal::testing
