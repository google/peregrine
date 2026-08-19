#include "src/internal/control/control.h"

#include <sys/socket.h>

#include <cstdint>
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
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

TEST(ControlTest, ResolvePeerHostInfo) {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  ASSERT_GT(port, 0);
  const HostInfo self = {
      .control_plane_listener =
          Endpoint::Create(absl::StrFormat("127.0.0.1:%d", port)),
  };
  auto server_or = GrpcServer::Create(self.control_plane_listener,
                                      grpc::InsecureServerCredentials());
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  Control ctrl(self, std::move(*server_or),
               grpc::InsecureChannelCredentials());  // NOLINT

  // Valid Endpoint's resolution is cached.
  const Endpoint remote_peer = Endpoint::Create("127.0.0.1:56789");
  auto host_or = ctrl.ResolvePeerHostInfo(remote_peer);
  ASSERT_TRUE(host_or.ok()) << host_or.status();

  const HostInfo& resolved_host = *host_or;
  EXPECT_EQ(resolved_host.control_plane_listener, remote_peer);
  ASSERT_EQ(resolved_host.data_plane_listeners.size(), 1);
  EXPECT_EQ(resolved_host.data_plane_listeners[0], remote_peer);

  // Re-resolve the same endpoint to verify the HostInfo is cached.
  auto host_cached_or = ctrl.ResolvePeerHostInfo(remote_peer);
  ASSERT_TRUE(host_cached_or.ok()) << host_cached_or.status();
  EXPECT_EQ(host_cached_or->control_plane_listener, remote_peer);
  ASSERT_EQ(host_cached_or->data_plane_listeners.size(), 1);
  EXPECT_EQ(host_cached_or->data_plane_listeners[0], remote_peer);

  // Uninitialized or Zero-Value Endpoints trigger an error.
  const Endpoint invalid_peer;
  auto fail_or = ctrl.ResolvePeerHostInfo(invalid_peer);
  EXPECT_FALSE(fail_or.ok());
  EXPECT_EQ(fail_or.status().code(), absl::StatusCode::kInvalidArgument);
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
