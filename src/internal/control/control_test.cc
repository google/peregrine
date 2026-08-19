#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

TEST(ControlTest, DynamicResolvePeerHostInfoBetweenNodes) {
  const uint16_t port_a = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port_a, 0);
  const HostInfo host_a = {
      .control_plane_listener =
          Endpoint::Create(absl::StrFormat("127.0.0.1:%d", port_a)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };

  const uint16_t port_b = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port_b, 0);
  const HostInfo host_b = {
      .control_plane_listener =
          Endpoint::Create(absl::StrFormat("127.0.0.1:%d", port_b)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20002")},
  };

  auto server_a_or = GrpcServer::Create(host_a.control_plane_listener,
                                        grpc::InsecureServerCredentials());
  ASSERT_TRUE(server_a_or.ok()) << server_a_or.status();
  Control ctrl_a(host_a, std::move(*server_a_or),
                 grpc::InsecureChannelCredentials());

  auto server_b_or = GrpcServer::Create(host_b.control_plane_listener,
                                        grpc::InsecureServerCredentials());
  ASSERT_TRUE(server_b_or.ok()) << server_b_or.status();
  Control ctrl_b(host_b, std::move(*server_b_or),
                 grpc::InsecureChannelCredentials());

  // 1. Node A dynamically resolves Node B via gRPC slow-path (cache miss).
  auto resolved_b_or =
      ctrl_a.ResolvePeerHostInfo(host_b.control_plane_listener);
  ASSERT_TRUE(resolved_b_or.ok()) << resolved_b_or.status();
  EXPECT_EQ(resolved_b_or->control_plane_listener,
            host_b.control_plane_listener);
  ASSERT_EQ(resolved_b_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(resolved_b_or->data_plane_listeners[0],
            host_b.data_plane_listeners[0]);

  // 2. Node A re-resolves Node B via cache fast-path.
  auto cached_b_or = ctrl_a.ResolvePeerHostInfo(host_b.control_plane_listener);
  ASSERT_TRUE(cached_b_or.ok()) << cached_b_or.status();
  EXPECT_EQ(cached_b_or->control_plane_listener, host_b.control_plane_listener);
  ASSERT_EQ(cached_b_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_b_or->data_plane_listeners[0],
            host_b.data_plane_listeners[0]);

  // 3. Node B automatically cached Node A from the inbound gRPC request.
  auto cached_a_or = ctrl_b.ResolvePeerHostInfo(host_a.control_plane_listener);
  ASSERT_TRUE(cached_a_or.ok()) << cached_a_or.status();
  EXPECT_EQ(cached_a_or->control_plane_listener, host_a.control_plane_listener);
  ASSERT_EQ(cached_a_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_a_or->data_plane_listeners[0],
            host_a.data_plane_listeners[0]);
}

TEST(ControlTest, ResolvePeerHostInfoErrors) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  const HostInfo self = {
      .control_plane_listener =
          Endpoint::Create(absl::StrFormat("127.0.0.1:%d", port)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };
  auto server_or = GrpcServer::Create(self.control_plane_listener,
                                      grpc::InsecureServerCredentials());
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  Control ctrl(self, std::move(*server_or), grpc::InsecureChannelCredentials());

  // Uninitialized or Zero-Value Endpoints trigger an error.
  const Endpoint invalid_peer;
  auto fail_or = ctrl.ResolvePeerHostInfo(invalid_peer);
  EXPECT_FALSE(fail_or.ok());
  EXPECT_EQ(fail_or.status().code(), absl::StatusCode::kInvalidArgument);

  // Non-existent peer endpoint triggers an unavailable error over gRPC.
  const uint16_t unused_port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(unused_port, 0);
  const Endpoint unreachable_peer =
      Endpoint::Create(absl::StrFormat("127.0.0.1:%d", unused_port));
  auto unreachable_or = ctrl.ResolvePeerHostInfo(unreachable_peer);
  EXPECT_FALSE(unreachable_or.ok());
  EXPECT_EQ(unreachable_or.status().code(), absl::StatusCode::kUnavailable);
}

TEST(ControlTest, HandleIncomingHostInfoRequest) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  const HostInfo server_host = {
      .control_plane_listener =
          Endpoint::Create(absl::StrFormat("127.0.0.1:%d", port)),
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:20001")},
  };

  auto server_or = GrpcServer::Create(server_host.control_plane_listener,
                                      grpc::InsecureServerCredentials());
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  Control ctrl(server_host, std::move(*server_or),
               grpc::InsecureChannelCredentials());
  const int server_port = ctrl.Port();
  ASSERT_NE(server_port, 0);

  const Endpoint client_ep = Endpoint::Create("127.0.0.1:56789");
  const HostInfo client_host = {
      .control_plane_listener = client_ep,
      .data_plane_listeners = {Endpoint::Create("127.0.0.1:30001")},
  };

  proto::ReqMsg req;
  ASSERT_TRUE(Message::Convert(client_host, req));

  const Endpoint server_ep =
      Endpoint::Create(absl::StrFormat("127.0.0.1:%d", server_port));
  const absl::StatusOr<proto::RespMsg> resp_or =
      ctrl.SendRequest(server_ep, req);
  ASSERT_TRUE(resp_or.ok()) << resp_or.status();

  HostInfo returned_server_host;
  ASSERT_TRUE(Message::Convert(*resp_or, returned_server_host));
  EXPECT_EQ(returned_server_host.control_plane_listener, server_ep);
  ASSERT_EQ(returned_server_host.data_plane_listeners.size(), 1);
  EXPECT_EQ(returned_server_host.data_plane_listeners[0],
            server_host.data_plane_listeners[0]);

  // Verify that the server cached the client's HostInfo in peer_hosts_.
  auto cached_client_or = ctrl.ResolvePeerHostInfo(client_ep);
  ASSERT_TRUE(cached_client_or.ok()) << cached_client_or.status();
  EXPECT_EQ(cached_client_or->control_plane_listener, client_ep);
  ASSERT_EQ(cached_client_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(cached_client_or->data_plane_listeners[0],
            client_host.data_plane_listeners[0]);
}

}  // namespace
}  // namespace peregrine::internal::testing
