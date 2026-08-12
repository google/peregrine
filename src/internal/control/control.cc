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
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kControl = "grpc control plane ";
}  // namespace

Control::Control(std::unique_ptr<GrpcServer> server,
                 std::shared_ptr<grpc::ChannelCredentials> client_creds)
    : grpc_server_(std::move(server)), client_creds_(std::move(client_creds)) {
  DCHECK(invariant());
  LOG(INFO) << kControl << "created";
}

Control::~Control() {
  if (grpc_server_ != nullptr) {
    grpc_server_->Shutdown();
    grpc_server_ = nullptr;
    LOG(INFO) << kControl << "destroyed";
  }
}

absl::StatusOr<std::unique_ptr<Control>> Control::Create(
    const Endpoint& self, RequestHandler&& handler,
    std::shared_ptr<grpc::ServerCredentials> server_creds,
    std::shared_ptr<grpc::ChannelCredentials> client_creds) {
  if (server_creds == nullptr)
    return absl::InvalidArgumentError("null server credentials");
  if (client_creds == nullptr)
    return absl::InvalidArgumentError("null client credentials");

  auto server =
      GrpcServer::Create(self, std::move(handler), std::move(server_creds));
  if (!server.ok()) return server.status();

  return std::unique_ptr<Control>(
      new Control(std::move(server).value(), std::move(client_creds)));
}

absl::StatusOr<proto::RespMsg> Control::SendRequest(const Endpoint& peer,
                                                    const proto::ReqMsg& req) {
  DCHECK(invariant());

  const GrpcClient& client = getOrCreateClient(peer);
  return client.SendUnary(req);
}

const GrpcClient& Control::getOrCreateClient(const Endpoint& peer) {
  DCHECK(invariant());

  absl::MutexLock _(peer_clients_mu_);
  std::unique_ptr<GrpcClient>& client = peer_clients_[peer];
  if ABSL_PREDICT_FALSE (client == nullptr) {
    client = std::make_unique<GrpcClient>(peer, client_creds_);
  }
  DCHECK_NE(client, nullptr);
  return *client;
}

}  // namespace peregrine::internal
