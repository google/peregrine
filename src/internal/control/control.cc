#include "src/internal/control/control.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/socket/psp/psp.h"

namespace peregrine::internal {

std::unique_ptr<Control> Control::Create(const Config& config,
                                         const HostInfo& self,
                                         SecurityCredentials creds) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  const Endpoint& e = self.control_plane_listener;
  if ABSL_PREDICT_FALSE (!e.HasNonzeroIpPort()) {
    LOG(ERROR) << "invalid control plane listener " << e;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!creds.IsValid()) {
    LOG(ERROR) << "invalid security credentials";
    return nullptr;
  }
  return absl::WrapUnique(new Control(config, self, creds));
}

bool Control::Start() {
  if (grpc_server_ != nullptr) return true;

  DCHECK(self_.IsValid());
  const Endpoint& e = self_.control_plane_listener;
  auto handler = [this](const proto::ReqMsg& req, proto::RespMsg* resp) {
    return this->handleAllRequest(req, resp);
  };
  if (auto server = GrpcServer::Create(e, server_creds_, std::move(handler));
      !server.ok()) {
    LOG(WARNING) << "failed to create grpc server @ " << e << ": "
                 << server.status();
    return false;
  } else {
    grpc_server_ = std::move(server).value();
    DCHECK(invariant());
    LOG(INFO) << "control started on " << e;
    return true;
  }
}

Control::~Control() {
  if (grpc_server_ == nullptr) return;

  grpc_server_->Shutdown();
  grpc_server_ = nullptr;
  LOG(INFO) << "destroyed";
}

// Server-side functions: begin

absl::Status Control::handleAllRequest(const proto::ReqMsg& req_msg,
                                       proto::RespMsg* resp_msg) {
  if (resp_msg == nullptr) {
    return absl::InvalidArgumentError("null response message");
  }
  switch (req_msg.msg_case()) {
    case proto::ReqMsg::kHostInfo:
      return handleHostInfoExchange(req_msg.host_info(),
                                    resp_msg->mutable_host_info());
    case proto::ReqMsg::kPspTcpReq:
      return handlePspTokenExchange(req_msg.psp_tcp_req(),
                                    resp_msg->mutable_psp_tcp_resp());
    case proto::ReqMsg::kRdmaConnReq:
      return handleRdmaConnect(req_msg.rdma_conn_req(),
                               resp_msg->mutable_rdma_conn_resp());
    // Append new request cases here.
    default:
      return absl::UnimplementedError("unsupported req type");
  }
}

absl::Status Control::handleHostInfoExchange(
    const proto::HostInfo& req_proto, proto::HostInfo* const resp_proto) {
  if ABSL_PREDICT_FALSE (resp_proto == nullptr) {
    return absl::InternalError("null host info resp proto");
  }

  HostInfo peer_info;
  if (!Message::Deserialize(req_proto, peer_info)) {
    return absl::InternalError("deserialize peer host info");
  }
  DCHECK(peer_info.IsValid());

  {
    absl::MutexLock _(peer_hosts_mu_);
    const Endpoint peer = peer_info.control_plane_listener;
    DCHECK(peer.HasNonzeroIpPort());
    peer_hosts_.insert_or_assign(peer, std::move(peer_info));
  }

  DCHECK(self_.IsValid());
  if (!Message::Serialize(self_, *resp_proto)) {
    return absl::InternalError("serialize self host info");
  }
  return absl::OkStatus();
}

absl::Status Control::handlePspTokenExchange(const proto::PspTcpReq& req_proto,
                                             proto::PspTcpResp* resp_proto) {
  if ABSL_PREDICT_FALSE (resp_proto == nullptr) {
    return absl::InternalError("null psp tcp resp proto");
  }

  PspToken peer_token;
  Endpoint self_target;
  if (!Message::Deserialize(req_proto, peer_token, self_target)) {
    return absl::InvalidArgumentError("deserialize peer psp tcp req proto");
  }
  DCHECK(peer_token.IsValid());
  DCHECK(self_target.HasNonzeroIpPort());

  absl::StatusOr<PspToken> self_token;
  {
    absl::MutexLock _(psp_handler_mu_);
    if (psp_tcp_handler_ == nullptr)
      return absl::UnimplementedError("missing psp tcp handler");
    self_token = psp_tcp_handler_(peer_token, self_target);
  }
  if (!self_token.ok()) {
    return self_token.status();
  } else if (!self_token->IsValid()) {
    return absl::InternalError("invalid self psp token");
  } else if (!Message::Serialize(*self_token, *resp_proto)) {
    return absl::InternalError("serialize self psp token");
  } else {
    return absl::OkStatus();
  }
}

absl::Status Control::handleRdmaConnect(const proto::RdmaConnReq& req_proto,
                                        proto::RdmaConnResp* resp_proto) {
  if ABSL_PREDICT_FALSE (resp_proto == nullptr) {
    return absl::InternalError("null rdma conn resp proto");
  }

  absl::MutexLock _(rdma_handler_mu_);
  if (rdma_conn_handler_ == nullptr) {
    return absl::UnimplementedError("missing rdma conn handler");
  }
  // TODO(mubashirq): revise this code following psp tcp above.
  // Use proto only in control.{h,cc}, not in rdma/ folder.
  return rdma_conn_handler_(req_proto, resp_proto);
}

// Server-side functions:end
// Client-side functions:begin

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

absl::StatusOr<proto::RespMsg> Control::sendRequest(
    const Endpoint& peer, const proto::ReqMsg& req_msg) {
  DCHECK(invariant());

  const GrpcClient& client = getOrCreateClient(peer);
  return client.SendUnary(req_msg);
}

absl::StatusOr<HostInfo> Control::GetPeerHostInfo(const Endpoint& peer) {
  if (!peer.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError("invalid peer endpoint");
  }

  // Fast-path: peer host info already cached.
  {
    absl::MutexLock _(peer_hosts_mu_);
    const auto it = peer_hosts_.find(peer);
    if ABSL_PREDICT_TRUE (it != peer_hosts_.end()) return it->second;
  }

  // Slow-path: fetch peer host info using grpc.
  proto::ReqMsg req_msg;
  if (auto* self = req_msg.mutable_host_info(); self == nullptr) {
    return absl::InternalError("no host info in req proto");
  } else if (!Message::Serialize(self_, *self)) {
    return absl::InternalError("serialize self host info");
  }

  HostInfo peer_info;
  if (const auto resp_msg = sendRequest(peer, req_msg); !resp_msg.ok()) {
    return resp_msg.status();
  } else if (!resp_msg->has_host_info()) {
    return absl::InternalError("no host info in resp proto");
  } else if (!Message::Deserialize(resp_msg->host_info(), peer_info)) {
    return absl::InternalError("deserialize peer host info proto");
  } else {
    DCHECK(peer_info.IsValid());
    DCHECK_EQ(peer_info.control_plane_listener, peer);
    {
      absl::MutexLock _(peer_hosts_mu_);
      peer_hosts_.insert_or_assign(peer, peer_info);
    }
    return peer_info;
  }
}

absl::StatusOr<PspToken> Control::ExchangePspTokens(const PspToken& self_token,
                                                    const Endpoint& peer_target,
                                                    const Endpoint& peer) {
  if (!peer.HasNonzeroIpPort())
    return absl::InvalidArgumentError("invalid peer");
  if (!peer_target.HasNonzeroIpPort())
    return absl::InvalidArgumentError("invalid peer target");
  if (!self_token.IsValid())
    return absl::InvalidArgumentError("invalid self psp token");

  proto::ReqMsg req_msg;
  if (proto::PspTcpReq* req = req_msg.mutable_psp_tcp_req(); req == nullptr) {
    return absl::InternalError("null psp tcp proto in req msg");
  } else if (!Message::Serialize(self_token, peer_target, *req)) {
    return absl::InternalError("serialize self psp token");
  }

  PspToken peer_token;
  if (const auto resp_msg = sendRequest(peer, req_msg); !resp_msg.ok()) {
    return resp_msg.status();
  } else if (!resp_msg->has_psp_tcp_resp()) {
    return absl::InternalError("null psp tcp proto in resp msg");
  } else if (!Message::Deserialize(resp_msg->psp_tcp_resp(), peer_token)) {
    return absl::InternalError("deserialize peer psp token");
  } else {
    DCHECK(peer_token.IsValid());
    return peer_token;
  }
}

absl::StatusOr<proto::RdmaConnResp> Control::ConnectRdmaPeer(
    const Endpoint& peer, std::string_view device_name, uint32_t qpn,
    absl::Span<const uint8_t> gid, uint32_t psn, uint32_t rkey) {
  if (!peer.HasNonzeroIpPort())
    return absl::InvalidArgumentError("invalid peer endpoint");
  constexpr size_t kGidSize = 16;
  if (gid.size() != kGidSize)
    return absl::InvalidArgumentError("invalid GID size");

  proto::ReqMsg req_msg;
  if (auto* rdma = req_msg.mutable_rdma_conn_req(); rdma == nullptr) {
    return absl::InternalError("null rdma conn proto in req msg");
  } else {
    // TOOD(mubashirq): revise this code following psp tcp above.
    rdma->set_device_name(device_name);
    rdma->set_qpn(qpn);
    const char* gid_data = reinterpret_cast<const char*>(gid.data());
    rdma->set_gid(std::string_view(gid_data, gid.size()));
    rdma->set_psn(psn);
    if (rkey != 0) rdma->set_rkey(rkey);
  }

  if (const auto resp = sendRequest(peer, req_msg); !resp.ok()) {
    return resp.status();
  } else if (!resp->has_rdma_conn_resp()) {
    return absl::InternalError("null rdma conn proto in resp msg");
  } else {
    // TOOD(mubashirq): use Deserialize(...), do not return proto.
    return resp->rdma_conn_resp();
  }
}

// Client-side functions: end

}  // namespace peregrine::internal
