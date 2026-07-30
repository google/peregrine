#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal::testing {
namespace {

using ::absl::StatusCode::kPermissionDenied;

constexpr std::string_view kAddr = "127.0.0.1:0";
constexpr std::string_view kAddrPrefix = "127.0.0.1:";
static_assert(kAddr.starts_with(kAddrPrefix));

TEST(GrpcTest, SuccessfulRequest) {
  bool callback_invoked = false;
  auto handler = [&](const proto::ReqMsg& req, proto::RespMsg* resp) {
    callback_invoked = true;
    EXPECT_TRUE(req.has_peer_requests());
    return absl::OkStatus();
  };

  auto creds = grpc::InsecureServerCredentials();
  auto server_or = GrpcServer::Create(kAddr, handler, creds);
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  ASSERT_GT(server->port(), 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, server->port());
  const GrpcClient client(server_addr, grpc::InsecureChannelCredentials());

  proto::ReqMsg req;
  req.mutable_peer_requests();
  const absl::StatusOr<proto::RespMsg> resp_or = client.SendUnary(req);
  EXPECT_TRUE(resp_or.ok());
  EXPECT_TRUE(callback_invoked);
}

TEST(GrpcTest, FailedRequest) {
  constexpr std::string_view kErrMsg = "Rejected peer control token";
  auto handler = [kErrMsg](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::PermissionDeniedError(kErrMsg);
  };

  auto creds = grpc::InsecureServerCredentials();
  auto server_or = GrpcServer::Create(kAddr, handler, creds);
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  ASSERT_GT(server->port(), 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, server->port());
  const GrpcClient client(server_addr, grpc::InsecureChannelCredentials());

  const proto::ReqMsg req;
  const absl::StatusOr<proto::RespMsg> resp_or = client.SendUnary(req);
  EXPECT_FALSE(resp_or.ok());
  const absl::Status s = resp_or.status();
  EXPECT_EQ(s.code(), kPermissionDenied);
  EXPECT_NE(s.message().find(kErrMsg), std::string::npos);
}

}  // namespace
}  // namespace peregrine::internal::testing
