#include "src/internal/control/grpc_server.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.pb.h"

namespace peregrine::internal {

namespace {
grpc::Status InvalidArgumentError(const std::string& msg) {
  return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, msg);
}
grpc::Status ToGrpcStatus(const absl::Status& s) {
  return grpc::Status(static_cast<grpc::StatusCode>(s.code()),
                      std::string(s.message()));
}
}  // namespace

void GrpcServer::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_ = nullptr;
    LOG(INFO) << "GrpcServer shutdown on port " << port_;
  }
}

absl::StatusOr<std::unique_ptr<GrpcServer>> GrpcServer::Create(
    const Endpoint& self, std::shared_ptr<grpc::ServerCredentials> creds,
    RequestHandler&& handler) {
  if ABSL_PREDICT_FALSE (creds == nullptr)
    return absl::InvalidArgumentError("server credentials must not be null");
  if ABSL_PREDICT_FALSE (handler == nullptr)
    return absl::InvalidArgumentError("request handler must not be null");

  std::unique_ptr<GrpcServer> server(new GrpcServer(std::move(handler)));

  int port = 0;
  const std::string addr = self.ToString();
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, std::move(creds), &port);
  builder.RegisterService(server.get());

  server->server_ = builder.BuildAndStart();
  if ABSL_PREDICT_FALSE (server->server_ == nullptr) {
    return absl::InternalError(
        absl::StrCat("GrpcServer failed to start on ", addr));
  }

  DCHECK_GT(port, 0);
  server->port_ = port;
  LOG(INFO) << "GrpcServer listening on port " << port;
  DCHECK(server->invariant());
  return server;
}

grpc::Status GrpcServer::ProcessUnary(grpc::ServerContext* context,
                                      const proto::ReqMsg* request,
                                      proto::RespMsg* response) {
  if ABSL_PREDICT_FALSE (context == nullptr)
    return InvalidArgumentError("null context");
  if ABSL_PREDICT_FALSE (request == nullptr)
    return InvalidArgumentError("null request");
  if ABSL_PREDICT_FALSE (response == nullptr)
    return InvalidArgumentError("null response");

  DCHECK(invariant());
  const absl::Status s = handler_(*request, response);
  if ABSL_PREDICT_FALSE (!s.ok()) return ToGrpcStatus(s);
  return grpc::Status::OK;
}

}  // namespace peregrine::internal
