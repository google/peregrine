#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

class ControlTest : public ::testing::Test {
 protected:
  ControlTest()
      : config_({.num_conns_per_peer = 1}),
        creds_a_({.server_creds = grpc::InsecureServerCredentials(),
                  .client_creds = grpc::InsecureChannelCredentials()}),
        creds_b_({.server_creds = grpc::InsecureServerCredentials(),
                  .client_creds = grpc::InsecureChannelCredentials()}) {}

 protected:
  const Config config_;
  const SecurityCredentials creds_a_;
  const SecurityCredentials creds_b_;
};

TEST_F(ControlTest, DynamicGetPeerHostInfoBetweenNodes) {
  const uint16_t port_a = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port_a, 0);
  HostInfo host_a = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port_a)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };

  const uint16_t port_b = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port_b, 0);
  HostInfo host_b = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port_b)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20002")},
  };

  auto ctrl_a = Control::Create(config_, host_a, creds_a_);
  ASSERT_NE(ctrl_a, nullptr);
  ASSERT_TRUE(ctrl_a->Start());

  auto ctrl_b = Control::Create(config_, host_b, creds_b_);
  ASSERT_NE(ctrl_b, nullptr);
  ASSERT_TRUE(ctrl_b->Start());

  // 1. Node A dynamically resolves Node B via gRPC slow-path (cache miss).
  auto resolved_b_or = ctrl_a->GetPeerHostInfo(host_b.control_plane_listener);
  ASSERT_TRUE(resolved_b_or.ok()) << resolved_b_or.status();
  EXPECT_EQ(resolved_b_or->control_plane_listener,
            host_b.control_plane_listener);
  ASSERT_EQ(resolved_b_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(resolved_b_or->data_plane_listeners[0],
            host_b.data_plane_listeners[0]);

  // 2. Node A re-resolves Node B via cache fast-path.
  auto cached_b_or = ctrl_a->GetPeerHostInfo(host_b.control_plane_listener);
  ASSERT_TRUE(cached_b_or.ok()) << cached_b_or.status();
  EXPECT_EQ(cached_b_or->control_plane_listener, host_b.control_plane_listener);
  ASSERT_EQ(cached_b_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_b_or->data_plane_listeners[0],
            host_b.data_plane_listeners[0]);

  // 3. Node B automatically cached Node A from the inbound gRPC request.
  auto cached_a_or = ctrl_b->GetPeerHostInfo(host_a.control_plane_listener);
  ASSERT_TRUE(cached_a_or.ok()) << cached_a_or.status();
  EXPECT_EQ(cached_a_or->control_plane_listener, host_a.control_plane_listener);
  ASSERT_EQ(cached_a_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_a_or->data_plane_listeners[0],
            host_a.data_plane_listeners[0]);
}

TEST_F(ControlTest, GetPeerHostInfoErrors) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  HostInfo self = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };
  auto ctrl = Control::Create(config_, self, creds_a_);
  ASSERT_NE(ctrl, nullptr);
  ASSERT_TRUE(ctrl->Start());

  // Uninitialized or Zero-Value Endpoints trigger an error.
  const Endpoint invalid_peer;
  auto fail_or = ctrl->GetPeerHostInfo(invalid_peer);
  EXPECT_FALSE(fail_or.ok());
  EXPECT_EQ(fail_or.status().code(), absl::StatusCode::kInvalidArgument);

  // Non-existent peer endpoint triggers an unavailable error over gRPC.
  const uint16_t unused_port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(unused_port, 0);
  const Endpoint unreachable_peer =
      Endpoint::Create(absl::StrCat("127.0.0.1:", unused_port));
  auto unreachable_or = ctrl->GetPeerHostInfo(unreachable_peer);
  EXPECT_FALSE(unreachable_or.ok());
  EXPECT_EQ(unreachable_or.status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(ControlTest, HandleIncomingHostInfoRequest) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  HostInfo server_host = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };

  auto ctrl = Control::Create(config_, server_host, creds_a_);
  ASSERT_NE(ctrl, nullptr);
  ASSERT_TRUE(ctrl->Start());

  const Endpoint client_ep = Endpoint::Create("127.0.0.1:56789");
  const HostInfo client_host = {
      .control_plane_listener = client_ep,
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:30001")},
  };

  proto::ReqMsg req;
  ASSERT_TRUE(Message::Convert(client_host, req));

  const absl::StatusOr<proto::RespMsg> resp_or =
      ctrl->SendRequest(server_host.control_plane_listener, req);
  ASSERT_TRUE(resp_or.ok()) << resp_or.status();

  HostInfo returned_server_host;
  ASSERT_TRUE(Message::Convert(*resp_or, returned_server_host));
  EXPECT_EQ(returned_server_host.control_plane_listener,
            server_host.control_plane_listener);
  ASSERT_EQ(returned_server_host.data_plane_listeners.size(), 1);
  EXPECT_EQ(returned_server_host.data_plane_listeners[0],
            server_host.data_plane_listeners[0]);

  // Verify that the server cached the client's HostInfo in peer_hosts_.
  auto cached_client_or = ctrl->GetPeerHostInfo(client_ep);
  ASSERT_TRUE(cached_client_or.ok()) << cached_client_or.status();
  EXPECT_EQ(cached_client_or->control_plane_listener, client_ep);
  ASSERT_EQ(cached_client_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_client_or->data_plane_listeners[0],
            client_host.data_plane_listeners[0]);
}

TEST_F(ControlTest, ConnectRdmaPeer) {
  const uint16_t port_a = util::FindFreePort(AF_INET, /*tcp=*/true);
  const uint16_t port_b = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port_a, 0);
  ASSERT_GT(port_b, 0);

  const HostInfo host_a = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port_a)),
      .rdma_interfaces = {{.name = "irdma0", .gid = std::string(16, '\0')}},
  };
  const HostInfo host_b = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port_b)),
      .rdma_interfaces = {{.name = "irdma0", .gid = std::string(16, '\0')}},
  };

  auto ctrl_a = Control::Create(config_, host_a, creds_a_);
  ASSERT_NE(ctrl_a, nullptr);
  ASSERT_TRUE(ctrl_a->Start());

  auto ctrl_b = Control::Create(config_, host_b, creds_b_);
  ASSERT_NE(ctrl_b, nullptr);
  ASSERT_TRUE(ctrl_b->Start());

  // Register an RDMA connect handler on Node B.
  ctrl_b->SetRdmaConnectHandler(
      [](const proto::RdmaConnectRequest& req,
         proto::RdmaConnectResponse* resp) -> absl::Status {
        EXPECT_EQ(req.device_name(), "irdma0");
        EXPECT_EQ(req.qpn(), 100);
        EXPECT_EQ(req.psn(), 0x123456);
        EXPECT_EQ(req.rkey(), 0xDEADBEEF);
        resp->set_qpn(200);
        resp->set_gid(req.gid());
        resp->set_psn(0x654321);
        resp->set_rkey(0xCAFEBABE);
        return absl::OkStatus();
      });

  const std::vector<uint8_t> dummy_gid(16, 0xAB);
  auto resp_or = ctrl_a->ConnectRdmaPeer(host_b.control_plane_listener,
                                         "irdma0", 100, dummy_gid, 0x123456,
                                         /*rkey=*/0xDEADBEEF);
  ASSERT_TRUE(resp_or.ok()) << resp_or.status();
  EXPECT_EQ(resp_or->qpn(), 200);
  EXPECT_EQ(resp_or->psn(), 0x654321);
  EXPECT_EQ(resp_or->rkey(), 0xCAFEBABE);
  EXPECT_EQ(resp_or->gid(),
            std::string_view(reinterpret_cast<const char*>(dummy_gid.data()),
                             dummy_gid.size()));

  // Invalid GID size fails fast client-side.
  const std::vector<uint8_t> invalid_gid(10, 0xAB);
  EXPECT_FALSE(ctrl_a
                   ->ConnectRdmaPeer(host_b.control_plane_listener, "irdma0",
                                     100, invalid_gid, 0x123456)
                   .ok());
}

TEST_F(ControlTest, ExchangePspKeyArgumentValidation) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  HostInfo self = {
      .control_plane_listener =
          Endpoint::Create(absl::StrCat("127.0.0.1:", port)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };
  auto ctrl = Control::Create(config_, self, creds_a_);
  ASSERT_NE(ctrl, nullptr);

  const Endpoint valid_peer = Endpoint::Create("127.0.0.1:12345");
  const Endpoint invalid_peer;
  const Endpoint target = Endpoint::Create("127.0.0.1:20002");

  // Invalid peer endpoint.
  EXPECT_EQ(ctrl->ExchangePspKey(invalid_peer,
                                 {.spi = 1, .key = std::string(16, 'a')},
                                 target)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // Invalid target endpoint.
  const Endpoint invalid_target;
  EXPECT_EQ(ctrl->ExchangePspKey(valid_peer,
                                 {.spi = 1, .key = std::string(16, 'a')},
                                 invalid_target)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // Invalid SPI (0).
  EXPECT_EQ(ctrl->ExchangePspKey(valid_peer,
                                 {.spi = 0, .key = std::string(16, 'a')},
                                 target)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // Invalid key size (short).
  EXPECT_EQ(ctrl->ExchangePspKey(valid_peer, {.spi = 1, .key = "short"}, target)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);

  // Invalid key size (long).
  EXPECT_EQ(ctrl->ExchangePspKey(valid_peer,
                                 {.spi = 1, .key = std::string(32, 'a')},
                                 target)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace peregrine::internal::testing
