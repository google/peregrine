#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal::testing {
namespace {

TEST(ControlTest, SendPeerRequestsLoopback) {
  bool callback_invoked = false;
  auto handler = [&](const proto::ReqMsg& req, proto::RespMsg* resp) {
    callback_invoked = true;
    EXPECT_TRUE(req.has_peer_requests());
    return absl::OkStatus();
  };

  const HostInfo self = {.control_plane_listener =
                             Endpoint::Create("127.0.0.1:0")};
  auto ctrl_or = Control::Create(self, std::move(handler),
                                 grpc::InsecureServerCredentials(),
                                 grpc::InsecureChannelCredentials());  // NOLINT
  ASSERT_TRUE(ctrl_or.ok()) << ctrl_or.status();
  std::unique_ptr<Control> ctrl = std::move(*ctrl_or);
  const int port = ctrl->Port();
  ASSERT_NE(port, 0);

  const Endpoint c = Endpoint::Create("127.0.0.1:56789");
  const HostInfo host = {.control_plane_listener = c};
  const Request req_item = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1000),
      .raddr = reinterpret_cast<Byte*>(0x2000),
      .len = 300,
  };
  const std::vector<Request> requests = {req_item};

  const std::string p = absl::StrFormat("127.0.0.1:%d", port);
  const Endpoint peer = Endpoint::Create(p);
  proto::ReqMsg req;
  ASSERT_TRUE(Message::Convert(host, requests, req));
  const absl::StatusOr<proto::RespMsg> resp_or = ctrl->SendRequest(peer, req);
  EXPECT_TRUE(resp_or.ok()) << resp_or.status();
  EXPECT_TRUE(callback_invoked);
}

}  // namespace
}  // namespace peregrine::internal::testing
