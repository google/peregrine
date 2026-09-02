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
#include "grpcpp/server_context.h"
#include "src/internal/base/endpoint.h"
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

  const Endpoint self = Endpoint::Create(kAddr);
  auto creds = grpc::InsecureServerCredentials();
  auto dummy_psp = [](const proto::PspKeyExchangeRequest&,
                      proto::PspKeyExchangeResponse*) {
    return absl::OkStatus();
  };
  auto server_or = GrpcServer::Create(self, creds, std::move(handler),
                                      std::move(dummy_psp));
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  const int port = server->Port();
  ASSERT_GT(port, 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, port);
  const Endpoint peer = Endpoint::Create(server_addr);
  const GrpcClient client(peer, grpc::InsecureChannelCredentials());

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

  const Endpoint self = Endpoint::Create(kAddr);
  auto creds = grpc::InsecureServerCredentials();
  auto dummy_psp = [](const proto::PspKeyExchangeRequest&,
                      proto::PspKeyExchangeResponse*) {
    return absl::OkStatus();
  };
  auto server_or = GrpcServer::Create(self, creds, std::move(handler),
                                      std::move(dummy_psp));
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  const int port = server->Port();
  ASSERT_GT(port, 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, port);
  const Endpoint peer = Endpoint::Create(server_addr);
  const GrpcClient client(peer, grpc::InsecureChannelCredentials());

  const proto::ReqMsg req;
  const absl::StatusOr<proto::RespMsg> resp_or = client.SendUnary(req);
  EXPECT_FALSE(resp_or.ok());
  const absl::Status s = resp_or.status();
  EXPECT_EQ(s.code(), kPermissionDenied);
  EXPECT_NE(s.message().find(kErrMsg), std::string::npos);
}

TEST(GrpcTest, NullCredentialsReturnsInvalidArgument) {
  const Endpoint self = Endpoint::Create(kAddr);
  auto valid_handler = [](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::OkStatus();
  };
  auto dummy_psp = [](const proto::PspKeyExchangeRequest&,
                      proto::PspKeyExchangeResponse*) {
    return absl::OkStatus();
  };

  auto null_creds_or =
      GrpcServer::Create(self, /*creds=*/nullptr, std::move(valid_handler),
                         std::move(dummy_psp));
  EXPECT_FALSE(null_creds_or.ok());
  EXPECT_EQ(null_creds_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(GrpcTest, NullHandlerReturnsInvalidArgument) {
  const Endpoint self = Endpoint::Create(kAddr);
  auto valid_creds = grpc::InsecureServerCredentials();
  auto dummy_psp = [](const proto::PspKeyExchangeRequest&,
                      proto::PspKeyExchangeResponse*) {
    return absl::OkStatus();
  };

  auto null_handler_or =
      GrpcServer::Create(self, valid_creds, /*handler=*/nullptr,
                         std::move(dummy_psp));
  EXPECT_FALSE(null_handler_or.ok());
  EXPECT_EQ(null_handler_or.status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(GrpcTest, SuccessfulExchangePspKey) {
  auto handler = [](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::OkStatus();
  };
  auto psp_handler = [](const proto::PspKeyExchangeRequest& req,
                        proto::PspKeyExchangeResponse* resp) {
    EXPECT_EQ(req.psp().spi(), 0x12345678);
    EXPECT_EQ(req.psp().key(), "0123456789abcdef");
    resp->mutable_psp()->set_spi(0x87654321);
    resp->mutable_psp()->set_key("fedcba9876543210");
    return absl::OkStatus();
  };

  const Endpoint self = Endpoint::Create(kAddr);
  auto creds = grpc::InsecureServerCredentials();
  auto server_or = GrpcServer::Create(self, creds, std::move(handler),
                                      std::move(psp_handler));
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  const int port = server->Port();
  ASSERT_GT(port, 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, port);
  const Endpoint peer = Endpoint::Create(server_addr);
  const GrpcClient client(peer, grpc::InsecureChannelCredentials());

  proto::PspKeyExchangeRequest req;
  req.mutable_psp()->set_spi(0x12345678);
  req.mutable_psp()->set_key("0123456789abcdef");

  auto resp_or = client.ExchangePspKey(req);
  ASSERT_TRUE(resp_or.ok()) << resp_or.status();
  EXPECT_EQ(resp_or->psp().spi(), 0x87654321);
  EXPECT_EQ(resp_or->psp().key(), "fedcba9876543210");
}

TEST(GrpcTest, InvalidPspKeyExchangeArguments) {
  auto handler = [](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::OkStatus();
  };
  auto psp_handler = [](const proto::PspKeyExchangeRequest& req,
                        proto::PspKeyExchangeResponse* resp) {
    if (req.psp().spi() == 0) {
      return absl::InvalidArgumentError("client_spi must be non-zero");
    }
    if (req.psp().key().size() != 16) {
      return absl::InvalidArgumentError("client_key must be exactly 16 bytes");
    }
    resp->mutable_psp()->set_spi(0x87654321);
    resp->mutable_psp()->set_key("fedcba9876543210");
    return absl::OkStatus();
  };

  const Endpoint self = Endpoint::Create(kAddr);
  auto creds = grpc::InsecureServerCredentials();
  auto server_or = GrpcServer::Create(self, creds, std::move(handler),
                                      std::move(psp_handler));
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);
  const int port = server->Port();
  ASSERT_GT(port, 0);

  const std::string server_addr = absl::StrCat(kAddrPrefix, port);
  const Endpoint peer = Endpoint::Create(server_addr);
  const GrpcClient client(peer, grpc::InsecureChannelCredentials());

  // Zero SPI
  proto::PspKeyExchangeRequest req1;
  req1.mutable_psp()->set_spi(0);
  req1.mutable_psp()->set_key("0123456789abcdef");
  auto resp1 = client.ExchangePspKey(req1);
  EXPECT_FALSE(resp1.ok());
  EXPECT_EQ(resp1.status().code(), absl::StatusCode::kInvalidArgument);

  // Key length != 16 bytes
  proto::PspKeyExchangeRequest req2;
  req2.mutable_psp()->set_spi(0x12345678);
  req2.mutable_psp()->set_key("short_key");
  auto resp2 = client.ExchangePspKey(req2);
  EXPECT_FALSE(resp2.ok());
  EXPECT_EQ(resp2.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(GrpcTest, ExchangePspKeyNullPointersReturnInvalidArgument) {
  auto handler = [](const proto::ReqMsg&, proto::RespMsg*) {
    return absl::OkStatus();
  };
  auto psp_handler = [](const proto::PspKeyExchangeRequest&,
                        proto::PspKeyExchangeResponse*) {
    return absl::OkStatus();
  };

  const Endpoint self = Endpoint::Create(kAddr);
  auto creds = grpc::InsecureServerCredentials();
  auto server_or = GrpcServer::Create(self, creds, std::move(handler),
                                      std::move(psp_handler));
  ASSERT_TRUE(server_or.ok()) << server_or.status();
  std::unique_ptr<GrpcServer> server = std::move(*server_or);

  grpc::ServerContext context;
  proto::PspKeyExchangeRequest req;
  req.mutable_psp()->set_spi(0x12345678);
  req.mutable_psp()->set_key("0123456789abcdef");
  proto::PspKeyExchangeResponse resp;

  // Null context.
  EXPECT_EQ(server->ExchangePspKey(nullptr, &req, &resp).error_code(),
            grpc::StatusCode::INVALID_ARGUMENT);

  // Null request.
  EXPECT_EQ(server->ExchangePspKey(&context, nullptr, &resp).error_code(),
            grpc::StatusCode::INVALID_ARGUMENT);

  // Null response.
  EXPECT_EQ(server->ExchangePspKey(&context, &req, nullptr).error_code(),
            grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace
}  // namespace peregrine::internal::testing
