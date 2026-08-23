#include "src/internal/control/control.h"

#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

std::unique_ptr<Control> Control::Create(
    const HostInfo& self, std::shared_ptr<grpc::ServerCredentials> server_creds,
    std::shared_ptr<grpc::ChannelCredentials> client_creds) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  if ABSL_PREDICT_FALSE (!self.control_plane_listener.HasNonzeroIpPort()) {
    LOG(WARNING) << "failed to create control: invalid control plane listener "
                 << self.control_plane_listener;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (server_creds == nullptr) {
    LOG(WARNING) << "failed to create control: null server credentials";
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (client_creds == nullptr) {
    LOG(WARNING) << "failed to create control: null client credentials";
    return nullptr;
  }

  return absl::WrapUnique(
      new Control(self, std::move(server_creds), std::move(client_creds)));
}

bool Control::Start() {
  DCHECK(self_.IsValid());

  if (grpc_server_ != nullptr) return true;

  const Endpoint& endpoint = self_.control_plane_listener;
  auto req_handler = [this](const proto::ReqMsg& req, proto::RespMsg* resp) {
    return handleIncomingRequest(req, resp);
  };
  auto grpc_server = GrpcServer::Create(endpoint, std::move(server_creds_),
                                        std::move(req_handler));
  if ABSL_PREDICT_FALSE (!grpc_server.ok()) {
    LOG(WARNING) << "failed to create grpc server on " << endpoint << ": "
                 << grpc_server.status();
    return false;
  }

  grpc_server_ = *std::move(grpc_server);
  DCHECK(invariant());
  LOG(INFO) << "control started on " << endpoint;
  return true;
}

Control::~Control() {
  if (grpc_server_ != nullptr) {
    grpc_server_->Shutdown();
    grpc_server_ = nullptr;
    LOG(INFO) << "destroyed";
  }
}

absl::Status Control::handleIncomingRequest(const proto::ReqMsg& req,
                                            proto::RespMsg* resp) {
  if (req.has_host_info()) {
    return handleHostInfo(req, resp);
  }
  return absl::UnimplementedError("unsupported request type");
}

absl::Status Control::handleHostInfo(const proto::ReqMsg& req,
                                     proto::RespMsg* resp) {
  HostInfo peer_info;
  if ABSL_PREDICT_FALSE (!Message::Convert(req, peer_info)) {
    return absl::InvalidArgumentError("malformed host info in request");
  }
  DCHECK(peer_info.IsValid());
  {
    absl::MutexLock _(peer_hosts_mu_);
    const Endpoint& peer = peer_info.control_plane_listener;
    peer_hosts_.insert_or_assign(peer, std::move(peer_info));
  }
  if ABSL_PREDICT_FALSE (resp == nullptr) {
    return absl::InternalError("null response message");
  }
  if ABSL_PREDICT_FALSE (!Message::Convert(self_, *resp)) {
    return absl::InternalError("failed to serialize self host info");
  }
  return absl::OkStatus();
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

absl::StatusOr<HostInfo> Control::GetPeerHostInfo(const Endpoint& peer) {
  if ABSL_PREDICT_FALSE (!peer.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError("invalid peer endpoint");
  }

  // Fast-path: peer host info already cached.
  {
    absl::MutexLock _(peer_hosts_mu_);
    const auto it = peer_hosts_.find(peer);
    if ABSL_PREDICT_TRUE (it != peer_hosts_.end()) return it->second;
  }

  // Slow-path: use gRPC to fetch peer host info.
  proto::ReqMsg req;
  if ABSL_PREDICT_FALSE (!Message::Convert(self_, req)) {
    return absl::InternalError(
        "failed to serialize self host info into request");
  }

  absl::StatusOr<proto::RespMsg> resp = SendRequest(peer, req);
  if ABSL_PREDICT_FALSE (!resp.ok()) {
    return resp.status();
  }

  HostInfo peer_info;
  if ABSL_PREDICT_FALSE (!Message::Convert(*resp, peer_info)) {
    return absl::InternalError(
        "failed to deserialize peer host info from response");
  }
  DCHECK(peer_info.IsValid());

  absl::MutexLock _(peer_hosts_mu_);
  peer_hosts_.insert_or_assign(peer, peer_info);
  return peer_info;
}

}  // namespace peregrine::internal
