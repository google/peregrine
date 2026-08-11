#include "src/internal/control/control.h"

#include <netinet/in.h>

#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kControl = "control plane ";
}  // namespace

Control::Control(std::unique_ptr<GrpcServer> server,
                 std::shared_ptr<grpc::ChannelCredentials> client_creds)
    : grpc_server_(std::move(server)), client_creds_(std::move(client_creds)) {
  DCHECK(invariant());
  LOG(INFO) << kControl << "created in gRPC mode";
}

Control::~Control() {
  DCHECK(invariant());
  {
    absl::MutexLock _(mu_);
    if (grpc_server_ != nullptr) {
      grpc_server_->Shutdown();
    }
  }
  LOG(INFO) << kControl << "destroyed";
}

absl::StatusOr<std::unique_ptr<Control>> Control::Create(
    std::string_view listen_addr, RequestHandler&& handler,
    std::shared_ptr<grpc::ServerCredentials> server_creds,
    std::shared_ptr<grpc::ChannelCredentials> client_creds) {
  if (server_creds == nullptr)
    return absl::InvalidArgumentError("null server credentials");
  if (client_creds == nullptr)
    return absl::InvalidArgumentError("null client credentials");

  auto server = GrpcServer::Create(listen_addr, std::move(handler),
                                   std::move(server_creds));
  if (!server.ok()) return server.status();

  return std::unique_ptr<Control>(
      new Control(std::move(server).value(), std::move(client_creds)));
}

absl::StatusOr<proto::RespMsg> Control::SendRequest(std::string_view peer_addr,
                                                    const proto::ReqMsg& req) {
  DCHECK(invariant());

  GrpcClient* client = nullptr;
  {
    absl::MutexLock _(mu_);
    client = getOrCreateClient(peer_addr);
  }
  if ABSL_PREDICT_FALSE (client == nullptr) {
    return absl::InternalError(
        absl::StrCat("failed to create gRPC client for ", peer_addr));
  }
  return client->SendUnary(req);
}

GrpcClient* Control::getOrCreateClient(std::string_view peer_addr) {
  DCHECK(invariant());

  auto it = peer_clients_.find(peer_addr);
  if ABSL_PREDICT_FALSE (it == peer_clients_.end()) {
    auto [inserted_it, _] = peer_clients_.emplace(
        peer_addr, std::make_unique<GrpcClient>(peer_addr, client_creds_));
    return inserted_it->second.get();
  }
  return it->second.get();
}

}  // namespace peregrine::internal
