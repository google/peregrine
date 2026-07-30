#include "src/internal/control/rpc_server.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

RpcServer::RpcServer(RequestHandler handler) : handler_(std::move(handler)) {}

RpcServer::~RpcServer() { Shutdown(); }

void RpcServer::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_ = nullptr;
    LOG(INFO) << "RpcServer shut down on port " << port_;
  }
}

absl::StatusOr<std::unique_ptr<RpcServer>> RpcServer::Create(
    std::string_view listen_address, RequestHandler handler,
    std::shared_ptr<grpc::ServerCredentials> creds) {
  if (!handler) {
    return absl::InvalidArgumentError(
        "RpcServer requires a valid RequestHandler");
  }

  std::unique_ptr<RpcServer> server(new RpcServer(std::move(handler)));
  grpc::ServerBuilder builder;
  int selected_port = 0;
  builder.AddListeningPort(std::string(listen_address), std::move(creds),
                           &selected_port);
  builder.RegisterService(server.get());

  server->server_ = builder.BuildAndStart();
  if (server->server_ == nullptr) {
    return absl::InternalError(
        absl::StrFormat("Failed to start RpcServer on %s", listen_address));
  }
  server->port_ = selected_port;
  LOG(INFO) << "RpcServer started and listening on port " << selected_port;
  return server;
}

grpc::Status RpcServer::ProcessUnary(grpc::ServerContext* context,
                                     const proto::ReqMsg* request,
                                     proto::RespMsg* response) {
  if (request == nullptr || response == nullptr) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "Null protobuf parameters received");
  }
  if (!handler_) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "RpcServer handler is not configured");
  }

  const absl::Status status = handler_(*request, response);
  if (!status.ok()) {
    return grpc::Status(static_cast<grpc::StatusCode>(status.code()),
                        std::string(status.message()));
  }
  return grpc::Status::OK;
}

}  // namespace peregrine::internal
