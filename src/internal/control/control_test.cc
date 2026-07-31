#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::TestParamInfo;
using ::testing::Values;

using Param = std::tuple</*family=*/int>;

std::string ToString(const TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrFormat("IPv%d", family == AF_INET ? 4 : 6);
}

class ControlTest : public ::testing::TestWithParam<Param> {
 protected:
  ControlTest()
      : family_(std::get<0>(GetParam())),
        tmp_(CreateTcpChannelPair(family_)),
        local_(std::move(tmp_.sndr)),
        remote_(std::move(tmp_.rcvr)) {}

 protected:
  const int family_;
  ConnectedChannelPair tmp_;
  Control local_;
  Control remote_;
};

INSTANTIATE_TEST_SUITE_P(, ControlTest,
                         /*family=*/Values(AF_INET, AF_INET6), ToString);

TEST_P(ControlTest, SendRecv) {
  constexpr std::string_view kCntl = "127.0.0.1:56789";
  constexpr std::string_view kData = "127.0.0.1:37184";
  constexpr uint64_t kLaddr = 0x1000;
  constexpr uint64_t kRaddr = 0x2000;
  constexpr uint32_t kLen = 300;

  // Send a message.
  proto::ReqMsg msg_s;
  auto prs = msg_s.mutable_peer_requests();
  auto* sp = prs->mutable_peer();
  sp->mutable_control_plane_listener()->set_ip_port(kCntl);
  sp->add_data_plane_listeners()->set_ip_port(kData);
  auto rs = prs->add_requests();
  rs->set_op(proto::Request::READ);
  rs->set_laddr(kLaddr);
  rs->set_raddr(kRaddr);
  rs->set_len(kLen);
  ASSERT_TRUE(local_.EnqueueSend(msg_s));
  LOG(INFO) << "msg sent";

  // Receive the message.
  proto::ReqMsg msg_r;
  ASSERT_FALSE(msg_r.has_peer_requests());
  while (!remote_.DequeueRecv(msg_r)) {
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_TRUE(msg_r.has_peer_requests());
  LOG(INFO) << "msg rcvd";

  // Check the message.
  const proto::PeerRequests& prr = msg_r.peer_requests();
  const proto::HostInfo& rp = prr.peer();
  EXPECT_EQ(rp.control_plane_listener().ip_port(), kCntl);
  EXPECT_EQ(rp.data_plane_listeners_size(), 1);
  EXPECT_EQ(rp.data_plane_listeners(0).ip_port(), kData);
  EXPECT_EQ(prr.requests_size(), 1);
  EXPECT_TRUE(Message::AreEqual(prr.requests(0), *rs));
}

TEST(GrpcControlTest, SendPeerRequestsLoopback) {
  bool callback_invoked = false;
  auto handler = [&](const proto::ReqMsg& req, proto::RespMsg* resp) {
    callback_invoked = true;
    EXPECT_TRUE(req.has_peer_requests());
    return absl::OkStatus();
  };

  auto ctrl_or = Control::CreateGrpcControl(
      "127.0.0.1:0", std::move(handler), grpc::InsecureServerCredentials(),
      grpc::InsecureChannelCredentials());  // NOLINT
  ASSERT_TRUE(ctrl_or.ok()) << ctrl_or.status();
  std::unique_ptr<Control> ctrl = std::move(*ctrl_or);
  ASSERT_NE(ctrl->port(), 0);

  const Endpoint c = Endpoint::Create("127.0.0.1:56789");
  const HostInfo host = {.control_plane_listener = c};
  const Request req_item = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1000),
      .raddr = reinterpret_cast<Byte*>(0x2000),
      .len = 300,
  };
  const std::vector<Request> requests = {req_item};

  const std::string target_addr = absl::StrFormat("127.0.0.1:%d", ctrl->port());
  proto::ReqMsg req;
  ASSERT_TRUE(Message::Convert(host, requests, req));
  const absl::StatusOr<proto::RespMsg> resp_or =
      ctrl->SendRequest(target_addr, req);
  EXPECT_TRUE(resp_or.ok()) << resp_or.status();
  EXPECT_TRUE(callback_invoked);
}

}  // namespace
}  // namespace peregrine::internal::testing
