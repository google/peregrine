#include "src/internal/control/message.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/socket/psp/psp.h"

namespace peregrine::internal {

bool Message::Serialize(const HostInfo& host, absl::Span<const Request> reqs,
                        proto::ReqMsg& msg) {
  DCHECK(host.IsValid());
  DCHECK(IsValid(reqs));

  msg.Clear();
  proto::PeerReq* pr = msg.mutable_peer_req();
  auto* pp = pr->mutable_peer();
  if (!pp || !Serialize(host, *pp)) {
    return false;
  }
  for (const auto& req : reqs) {
    auto* proto = pr->add_reqs();
    if (!proto || !serialize(req, *proto)) return false;
  }
  return true;
}

std::pair<HostInfo, std::vector<Request>> Message::Deserialize(
    const proto::ReqMsg& msg) {
  DCHECK(msg.has_peer_req());

  const auto invalid = std::make_pair(HostInfo(), std::vector<Request>(0));

  HostInfo host;
  const proto::PeerReq& pr = msg.peer_req();
  if (!Deserialize(pr.peer(), host)) return invalid;
  DCHECK(host.IsValid());

  std::vector<Request> reqs;
  reqs.reserve(pr.reqs_size());
  Request req;
  for (const auto& proto : pr.reqs()) {
    if (!deserialize(proto, req)) return invalid;
    reqs.push_back(req);
  }
  if (!IsValid(reqs)) {
    return invalid;
  }
  return {host, std::move(reqs)};
}

bool Message::Serialize(const HostInfo& host, proto::HostInfo& proto) {
  if (!host.IsValid()) return false;

  auto* pc = proto.mutable_control_plane_listener();
  if (!pc || !serialize(host.control_plane_listener, *pc)) {
    return false;
  }
  for (const auto& e : host.data_plane_listeners) {
    auto* pd = proto.add_data_plane_listeners();
    if (!pd || !serialize(e, *pd)) return false;
  }
  for (const auto& r : host.rdma_nics) {
    proto::RdmaNic* pr = proto.add_rdma_nics();
    if (!pr || !serialize(r, *pr)) return false;
  }
  return true;
}

bool Message::Deserialize(const proto::HostInfo& proto, HostInfo& host) {
  if (proto.has_control_plane_listener()) {
    const proto::Endpoint& pc = proto.control_plane_listener();
    if (!deserialize(pc, host.control_plane_listener)) return false;
  }

  Endpoint e;
  host.data_plane_listeners.clear();
  host.data_plane_listeners.reserve(proto.data_plane_listeners_size());
  for (const auto& pd : proto.data_plane_listeners()) {
    if (!deserialize(pd, e)) return false;
    host.data_plane_listeners.push_back(e);
  }

  RdmaNic r;
  host.rdma_nics.clear();
  host.rdma_nics.reserve(proto.rdma_nics_size());
  for (const auto& pr : proto.rdma_nics()) {
    if (!deserialize(pr, r)) return false;
    host.rdma_nics.push_back(r);
  }

  return host.IsValid();
}

bool Message::Serialize(const PspToken& token, const Endpoint& peer_target,
                        proto::PspTcpReq& proto) {
  auto* psp = proto.mutable_psp();
  if (!psp || !serialize(token, *psp)) return false;

  auto* target = proto.mutable_peer_target();
  if (!target || !serialize(peer_target, *target)) return false;

  return true;
}

bool Message::Deserialize(const proto::PspTcpReq& proto, PspToken& token,
                          Endpoint& peer_target) {
  if (!proto.has_psp()) return false;
  if (!deserialize(proto.psp(), token)) return false;

  if (!proto.has_peer_target()) return false;
  if (!deserialize(proto.peer_target(), peer_target)) return false;

  return true;
}

bool Message::Serialize(const PspToken& token, proto::PspTcpResp& proto) {
  auto* psp = proto.mutable_psp();
  if (!psp || !serialize(token, *psp)) return false;
  return true;
}

bool Message::Deserialize(const proto::PspTcpResp& proto, PspToken& token) {
  if (!proto.has_psp()) return false;
  if (!deserialize(proto.psp(), token)) return false;
  return true;
}

// Below are for common message components

bool Message::serialize(const Request& req, proto::Request& proto) {
  if (!req.IsValid()) return false;

  DCHECK(req.op == Op::kRead || req.op == Op::kWrite);
  proto.set_op(req.op == Op::kRead ? proto::Request::READ
                                   : proto::Request::WRITE);
  proto.set_laddr(reinterpret_cast<uint64_t>(req.laddr));
  proto.set_raddr(reinterpret_cast<uint64_t>(req.raddr));
  proto.set_len(req.len);
  proto.set_rkey(req.rkey);
  return true;
}

bool Message::deserialize(const proto::Request& proto, Request& req) {
  const proto::Request::Op op = proto.op();
  if (op == proto::Request::INVALID) return false;

  DCHECK(op == proto::Request::READ || op == proto::Request::WRITE);
  req.op = op == proto::Request::READ ? Op::kRead : Op::kWrite;
  req.laddr = reinterpret_cast<Byte*>(proto.laddr());
  req.raddr = reinterpret_cast<Byte*>(proto.raddr());
  req.len = proto.len();
  req.rkey = proto.rkey();
  return req.IsValid();
}

bool Message::serialize(const Endpoint& endpoint, proto::Endpoint& proto) {
  if (!endpoint.HasNonzeroIpPort()) return false;

  proto.set_ip_port(endpoint.ToString());
  return true;
}

bool Message::deserialize(const proto::Endpoint& proto, Endpoint& endpoint) {
  if (!proto.has_ip_port()) return false;

  endpoint = Endpoint::Create(proto.ip_port());
  return endpoint.HasNonzeroIpPort();
}

bool Message::serialize(const PspToken& token, proto::PspToken& proto) {
  if (!token.IsValid()) return false;

  proto.set_spi(token.spi.value());
  proto.set_gen(token.gen.value());
  const char* data = reinterpret_cast<const char*>(token.key.data());
  proto.set_key(std::string_view(data, token.key.size()));
  return true;
}

bool Message::deserialize(const proto::PspToken& proto, PspToken& token) {
  if (!proto.has_spi()) return false;
  if (!proto.has_gen()) return false;
  if (!proto.has_key()) return false;
  if (proto.key().size() != token.key.size()) return false;

  token.spi = Spi(proto.spi());
  token.gen = Gen(proto.gen());
  std::memcpy(token.key.data(), proto.key().data(), token.key.size());
  return token.IsValid();
}

bool Message::serialize(const RdmaNic& rdma, proto::RdmaNic& proto) {
  if (!rdma.IsValid()) return false;

  proto.set_name(rdma.name);
  proto.set_gid(rdma.gid);
  proto.set_port(rdma.port);
  return true;
}

bool Message::deserialize(const proto::RdmaNic& proto, RdmaNic& rdma) {
  if (!proto.has_name()) return false;
  if (!proto.has_gid()) return false;
  if (!proto.has_port()) return false;

  rdma.name = proto.name();
  rdma.gid = proto.gid();
  rdma.port = proto.port();
  return rdma.IsValid();
}

}  // namespace peregrine::internal
