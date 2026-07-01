#include "src/internal/control/parser.h"

#include <cstddef>
#include <string>

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

TEST(ControlMsgTest, Request) {
  proto::ControlReq msg;
  auto pr = msg.mutable_peer_requests();
  pr->set_peer("127.0.0.1:12345");
  auto r = pr->add_reqs();
  r->set_op(proto::Request::READ);
  r->set_laddr(0x1000);
  r->set_raddr(0x2000);
  r->set_len(300);
  EXPECT_TRUE(msg.has_peer_requests());

  proto::ControlReq msg2;
  EXPECT_FALSE(msg2.has_peer_requests());
  EXPECT_FALSE(ControlMsg::Deserialize("bad", msg2));

  const std::string s = ControlMsg::Serialize(msg);
  const size_t size = s.size();
  LOG(INFO) << "serialized control message size: " << size;

  EXPECT_TRUE(ControlMsg::Deserialize(s, msg2));
  EXPECT_TRUE(msg2.has_peer_requests());

  const proto::PeerRequests pr2 = msg2.peer_requests();
  EXPECT_EQ(pr2.peer(), "127.0.0.1:12345");
  EXPECT_EQ(pr2.reqs_size(), 1);
  const proto::Request r2 = pr2.reqs(0);
  EXPECT_EQ(r2.op(), proto::Request::READ);
  EXPECT_EQ(r2.laddr(), 0x1000);
  EXPECT_EQ(r2.raddr(), 0x2000);
  EXPECT_EQ(r2.len(), 300);
}

}  // namespace
}  // namespace peregrine::internal::testing
