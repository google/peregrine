#include "src/internal/control/parser.h"

#include <string>

#include "gtest/gtest.h"

namespace peregrine::internal::testing {
namespace {

TEST(ControlMsgTest, Request) {
  proto::Control c;
  auto r = c.mutable_request();
  r->set_op(proto::Request::READ);
  r->set_laddr(0x1000);
  r->set_raddr(0x2000);
  r->set_len(300);
  EXPECT_TRUE(c.has_request());

  proto::Control c2;
  EXPECT_FALSE(c2.has_request());
  EXPECT_FALSE(ControlMsg::Deserialize("bad", c2));

  const std::string s = ControlMsg::Serialize(c);
  EXPECT_TRUE(ControlMsg::Deserialize(s, c2));
  EXPECT_TRUE(c2.has_request());

  const proto::Request r2 = c2.request();
  EXPECT_EQ(r2.op(), proto::Request::READ);
  EXPECT_EQ(r2.laddr(), 0x1000);
  EXPECT_EQ(r2.raddr(), 0x2000);
  EXPECT_EQ(r2.len(), 300);
}

}  // namespace
}  // namespace peregrine::internal::testing
