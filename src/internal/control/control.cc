#include "src/internal/control/control.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "infiniband/verbs.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

std::unique_ptr<Control> Control::Create(const Config& config,
                                         const HostInfo& self,
                                         SecurityCredentials creds) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  if ABSL_PREDICT_FALSE (!self.control_plane_listener.HasNonzeroIpPort()) {
    LOG(WARNING) << "failed to create control: invalid control plane listener "
                 << self.control_plane_listener;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!creds.IsValid()) {
    LOG(WARNING) << "failed to create control: invalid security credentials";
    return nullptr;
  }

  return absl::WrapUnique(new Control(config, self, creds));
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

void Control::SetRdmaConnectHandler(RdmaConnectHandler handler) {
  absl::MutexLock _(rdma_handler_mu_);
  rdma_connect_handler_ = std::move(handler);
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
  if (req.has_rdma_connect_req()) {
    return handleRdmaConnect(req, resp);
  }
  return absl::UnimplementedError("unsupported request type");
}

absl::Status Control::handleHostInfo(const proto::ReqMsg& req,
                                     proto::RespMsg* resp) {
  HostInfo peer_info;
  if ABSL_PREDICT_FALSE (!Message::Convert(req, peer_info)) {
    return absl::InternalError("failed to deserialize peer host info");
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

absl::Status Control::handleRdmaConnect(const proto::ReqMsg& req,
                                        proto::RespMsg* resp) {
  if (resp == nullptr) {
    return absl::InternalError("null response message");
  }
  proto::RdmaConnectResponse rdma_resp;
  absl::Status status;
  {
    absl::MutexLock _(rdma_handler_mu_);
    if (!rdma_connect_handler_) {
      return absl::FailedPreconditionError(
          "no RDMA connect handler registered");
    }
    status = rdma_connect_handler_(req.rdma_connect_req(), &rdma_resp);
  }
  if (!status.ok()) {
    return status;
  }
  *resp->mutable_rdma_connect_resp() = std::move(rdma_resp);
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

absl::StatusOr<proto::RdmaConnectResponse> Control::ConnectRdmaPeer(
    const Endpoint& peer, std::string_view device_name, uint32_t qpn,
    absl::Span<const uint8_t> gid, uint32_t psn) {
  if (!peer.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError("invalid peer endpoint");
  }
  if (gid.size() != sizeof(union ibv_gid)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("invalid GID size: expected %d bytes, got %d",
                        sizeof(union ibv_gid), gid.size()));
  }

  proto::ReqMsg req;
  proto::RdmaConnectRequest* connect_req = req.mutable_rdma_connect_req();
  connect_req->set_device_name(device_name);
  connect_req->set_qpn(qpn);
  connect_req->set_gid(
      std::string_view(reinterpret_cast<const char*>(gid.data()), gid.size()));
  connect_req->set_psn(psn);

  auto resp = SendRequest(peer, req);
  if (!resp.ok()) {
    return resp.status();
  }
  if (!resp->has_rdma_connect_resp()) {
    return absl::InternalError("missing RdmaConnectResponse in response");
  }
  return resp->rdma_connect_resp();
}

}  // namespace peregrine::internal
