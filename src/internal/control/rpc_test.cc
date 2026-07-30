#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/rpc_client.h"
#include "src/internal/control/rpc_server.h"

namespace peregrine::internal::testing {
namespace {

TEST(RpcTest, UnconfiguredHandlerFails) {
  const absl::StatusOr<std::unique_ptr<RpcServer>> server_or =
      RpcServer::Create("127.0.0.1:0", nullptr,
                        grpc::InsecureServerCredentials());  // NOLINT
  EXPECT_FALSE(server_or.ok());
  EXPECT_EQ(server_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(RpcTest, LoopbackUnaryQueryExecution) {
  bool callback_invoked = false;
  auto handler = [&](const proto::ReqMsg& req, proto::RespMsg* resp) {
    callback_invoked = true;
    EXPECT_TRUE(req.has_peer_requests());
    return absl::OkStatus();
  };

  absl::StatusOr<std::unique_ptr<RpcServer>> server_or =
      RpcServer::Create("127.0.0.1:0", handler,
                        grpc::InsecureServerCredentials());  // NOLINT
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<RpcServer> server = std::move(*server_or);
  ASSERT_NE(server->port(), 0);

  const std::string server_addr =
      absl::StrFormat("127.0.0.1:%d", server->port());
  RpcClient client(server_addr,
                   grpc::InsecureChannelCredentials());  // NOLINT

  proto::ReqMsg req;
  req.mutable_peer_requests();
  absl::StatusOr<proto::RespMsg> resp_or = client.SendUnary(req);
  EXPECT_TRUE(resp_or.ok()) << resp_or.status();
  EXPECT_TRUE(callback_invoked);
}

TEST(RpcTest, HandlerErrorReturnsGrpcFailureStatus) {
  auto error_handler = [](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::PermissionDeniedError("Rejected peer control token");
  };

  absl::StatusOr<std::unique_ptr<RpcServer>> server_or =
      RpcServer::Create("127.0.0.1:0", error_handler,
                        grpc::InsecureServerCredentials());  // NOLINT
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<RpcServer> server = std::move(*server_or);

  RpcClient client(absl::StrFormat("127.0.0.1:%d", server->port()),
                   grpc::InsecureChannelCredentials());  // NOLINT
  proto::ReqMsg req;
  absl::StatusOr<proto::RespMsg> resp_or = client.SendUnary(req);
  EXPECT_FALSE(resp_or.ok());
  EXPECT_EQ(resp_or.status().code(), absl::StatusCode::kPermissionDenied);
  EXPECT_NE(resp_or.status().message().find("Rejected peer control token"),
            std::string::npos);
}

}  // namespace
}  // namespace peregrine::internal::testing
