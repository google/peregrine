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
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kControl = "grpc control plane ";
}  // namespace

Control::Control(const HostInfo& self, std::unique_ptr<GrpcServer> server,
                 std::shared_ptr<grpc::ChannelCredentials> client_creds)
    : self_(self),
      grpc_server_(std::move(server)),
      client_creds_(std::move(client_creds)) {
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
    const HostInfo& self, RequestHandler&& handler,
    std::shared_ptr<grpc::ServerCredentials> server_creds,
    std::shared_ptr<grpc::ChannelCredentials> client_creds) {
  if (server_creds == nullptr)
    return absl::InvalidArgumentError("null server credentials");
  if (client_creds == nullptr)
    return absl::InvalidArgumentError("null client credentials");

  auto server = GrpcServer::Create(self.control_plane_listener,
                                   std::move(handler), std::move(server_creds));
  if (!server.ok()) return server.status();

  return std::unique_ptr<Control>(
      new Control(self, *std::move(server), std::move(client_creds)));
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

absl::StatusOr<HostInfo> Control::ResolvePeerHostInfo(
    const Endpoint& peer_control_ep) {
  if ABSL_PREDICT_FALSE (!peer_control_ep.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError("invalid peer endpoint");
  }

  absl::MutexLock _(peer_hosts_mu_);

  std::unique_ptr<HostInfo>& host_info = peer_hosts_[peer_control_ep];
  if ABSL_PREDICT_FALSE (host_info == nullptr) {
    // TODO: Future CL to dispatch out-of-band Unary gRPC ExchangeHostInfo
    // RPCs to remote peers. For now, we assume the control-plane listener is
    // also the data-plane listener.
    host_info = std::make_unique<HostInfo>(HostInfo{
        .control_plane_listener = peer_control_ep,
        .data_plane_listeners = {peer_control_ep},
    });
  }

  DCHECK_NE(host_info, nullptr);
  return *host_info;
}

}  // namespace peregrine::internal
