#include "src/internal/control/message.h"

#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
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
  const Endpoint e = Endpoint::Create("127.0.0.1:56789");

  const Request req = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(kLaddr),
      .raddr = reinterpret_cast<Byte*>(kRaddr),
      .len = kLen,
  };
  proto::ReqMsg a;
  Message::Convert(e, {req}, a);

  const std::string s = Message::Serialize(a);
  ASSERT_LE(s.size(), Message::kMaxLen);

  proto::ReqMsg b;
  ASSERT_FALSE(b.has_peer_requests());
  EXPECT_TRUE(Message::Deserialize(s, b));
  EXPECT_TRUE(b.has_peer_requests());
  EXPECT_EQ(b.peer_requests().peer(), e.ToString());
  EXPECT_EQ(b.peer_requests().requests_size(), 1);
  EXPECT_TRUE(Message::AreEqual(a.peer_requests().requests(0),
                                b.peer_requests().requests(0)));

  const auto [peer, requests] = Message::Convert(b);
  EXPECT_EQ(peer, e);
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0], req);
}

}  // namespace
}  // namespace peregrine::internal::testing
