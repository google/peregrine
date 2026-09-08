#include "src/internal/control/message.h"

#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"

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
      .rkey = 0xCAFE,
  };
  proto::ReqMsg a;
  Message::Serialize(host, {req}, a);

  const std::string s = Message::Serialize(a);
  proto::ReqMsg b;
  ASSERT_FALSE(b.has_peer_req());
  EXPECT_TRUE(Message::Deserialize(s, b));
  EXPECT_TRUE(b.has_peer_req());

  const auto [hb, requests] = Message::Deserialize(b);
  EXPECT_EQ(hb.control_plane_listener, c);
  EXPECT_EQ(hb.data_plane_listeners.size(), 2);
  EXPECT_EQ(hb.data_plane_listeners[0], d0);
  EXPECT_EQ(hb.data_plane_listeners[1], d1);
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0], req);
  EXPECT_EQ(requests[0].rkey, 0xCAFE);
}

TEST(MessageTest, HostInfoExchange) {
  // Construct a valid Multi-NIC HostInfo configuration.
  const Endpoint c = Endpoint::Create("10.0.0.1:10000");
  const Endpoint d0 = Endpoint::Create("10.0.0.1:35247");
  const Endpoint d1 = Endpoint::Create("10.0.0.2:51691");
  const std::string gid(16, '\xAB');
  const HostInfo source = {
      .control_plane_listener = c,
      .data_plane_listeners = {d0, d1},
      .rdma_nics =
          {
              RdmaNic{.name = "irdma0", .gid = gid, .port = 1},
              RdmaNic{.name = "irdma1", .gid = gid, .port = 2},
          },
  };
  ASSERT_TRUE(source.IsValid());

  proto::ReqMsg req;
  ASSERT_TRUE(Message::Serialize(source, *req.mutable_host_info()));
  EXPECT_TRUE(req.has_host_info());

  HostInfo host;
  ASSERT_TRUE(Message::Deserialize(req.host_info(), host));
  EXPECT_EQ(host.control_plane_listener, c);
  ASSERT_EQ(host.data_plane_listeners.size(), 2);
  EXPECT_EQ(host.data_plane_listeners[0], d0);
  EXPECT_EQ(host.data_plane_listeners[1], d1);
  ASSERT_EQ(host.rdma_nics.size(), 2);
  EXPECT_EQ(host.rdma_nics[0].name, "irdma0");
  EXPECT_EQ(host.rdma_nics[0].gid, gid);
  EXPECT_EQ(host.rdma_nics[0].port, 1);
  EXPECT_EQ(host.rdma_nics[1].name, "irdma1");
  EXPECT_EQ(host.rdma_nics[1].gid, gid);
  EXPECT_EQ(host.rdma_nics[1].port, 2);
}

TEST(MessageTest, RdmaConnectMessages) {
  const std::string dummy_gid(16, '\xCD');

  proto::ReqMsg req_msg;
  auto* req = req_msg.mutable_rdma_conn_req();
  req->set_device_name("irdma0");
  req->set_qpn(1234);
  req->set_gid(dummy_gid);
  req->set_psn(0);

  EXPECT_TRUE(req_msg.has_rdma_conn_req());
  EXPECT_EQ(req_msg.rdma_conn_req().device_name(), "irdma0");
  EXPECT_EQ(req_msg.rdma_conn_req().qpn(), 1234);
  EXPECT_EQ(req_msg.rdma_conn_req().gid(), dummy_gid);
  EXPECT_EQ(req_msg.rdma_conn_req().psn(), 0);

  proto::RespMsg resp_msg;
  auto* resp = resp_msg.mutable_rdma_conn_resp();
  resp->set_qpn(5678);
  resp->set_gid(dummy_gid);
  resp->set_psn(0);

  EXPECT_TRUE(resp_msg.has_rdma_conn_resp());
  EXPECT_EQ(resp_msg.rdma_conn_resp().qpn(), 5678);
  EXPECT_EQ(resp_msg.rdma_conn_resp().gid(), dummy_gid);
  EXPECT_EQ(resp_msg.rdma_conn_resp().psn(), 0);
}

TEST(MessageTest, PeerResponseMessages) {
  proto::RespMsg resp_msg;
  auto* peer_resp = resp_msg.mutable_peer_resp();
  peer_resp->add_rkeys(0x1111);
  peer_resp->add_rkeys(0x2222);

  EXPECT_TRUE(resp_msg.has_peer_resp());
  ASSERT_EQ(resp_msg.peer_resp().rkeys_size(), 2);
  EXPECT_EQ(resp_msg.peer_resp().rkeys(0), 0x1111);
  EXPECT_EQ(resp_msg.peer_resp().rkeys(1), 0x2222);
}

TEST(MessageTest, AreEqualAndRKey) {
  proto::Request r1;
  r1.set_op(proto::Request::READ);
  r1.set_laddr(0x1000);
  r1.set_raddr(0x2000);
  r1.set_len(1024);
  r1.set_rkey(0xABCD);

  proto::Request r2;
  r2.set_op(proto::Request::READ);
  r2.set_laddr(0x1000);
  r2.set_raddr(0x2000);
  r2.set_len(1024);
  r2.set_rkey(0xABCD);

  EXPECT_TRUE(Message::AreEqual(r1, r2));

  // Mismatched rkey
  proto::Request r3 = r1;
  r3.set_rkey(0x1234);
  EXPECT_FALSE(Message::AreEqual(r1, r3));

  // Mismatched op
  proto::Request r4 = r1;
  r4.set_op(proto::Request::WRITE);
  EXPECT_FALSE(Message::AreEqual(r1, r4));

  // Mismatched laddr
  proto::Request r5 = r1;
  r5.set_laddr(0x9999);
  EXPECT_FALSE(Message::AreEqual(r1, r5));

  // Mismatched raddr
  proto::Request r6 = r1;
  r6.set_raddr(0x9999);
  EXPECT_FALSE(Message::AreEqual(r1, r6));

  // Mismatched len
  proto::Request r7 = r1;
  r7.set_len(2048);
  EXPECT_FALSE(Message::AreEqual(r1, r7));
}

}  // namespace
}  // namespace peregrine::internal::testing
