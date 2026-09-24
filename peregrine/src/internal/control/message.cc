#include "peregrine/src/internal/control/message.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/control/message.pb.h"
#include "peregrine/src/internal/control/message_internal.pb.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal {

bool Message::Serialize(const HostInfo& host, absl::Span<const Request> reqs,
                        proto::ReqMsg& msg) {
  if (!host.IsValid()) return false;
  if (!IsValid(reqs)) return false;

  msg.Clear();
  proto::PeerReq* pr = msg.mutable_peer_req();
  if (!pr) return false;
  auto* pp = pr->mutable_peer();
  if (!pp || !Serialize(host, *pp)) return false;

  for (const auto& req : reqs) {
    auto* proto = pr->add_reqs();
    if (!proto || !serialize(req, *proto)) return false;
  }
  return true;
}

std::pair<HostInfo, std::vector<Request>> Message::Deserialize(
    const proto::ReqMsg& msg) {
  const auto invalid = std::make_pair(HostInfo(), std::vector<Request>(0));

  HostInfo host;
  if (!msg.has_peer_req()) return invalid;
  const proto::PeerReq& pr = msg.peer_req();
  if (!pr.has_peer()) return invalid;
  if (!Deserialize(pr.peer(), host)) return invalid;
  DCHECK(host.IsValid());

  std::vector<Request> reqs;
  reqs.reserve(pr.reqs_size());
  Request req;
  for (const auto& proto : pr.reqs()) {
    if (!deserialize(proto, req)) return invalid;
    reqs.push_back(req);
  }
  if (!IsValid(reqs)) return invalid;
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
  return true;
}

bool Message::Deserialize(const proto::HostInfo& proto, HostInfo& host) {
  if (!proto.has_control_plane_listener()) return false;
  const proto::Endpoint& pc = proto.control_plane_listener();
  if (!deserialize(pc, host.control_plane_listener)) return false;

  NicInfo nic;
  host.data_plane_listeners.clear();
  host.data_plane_listeners.reserve(proto.data_plane_listeners_size());
  for (const auto& pn : proto.data_plane_listeners()) {
    if (!deserialize(pn, nic)) return false;
    host.data_plane_listeners.push_back(nic);
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

bool Message::serialize(const NicInfo& nic, proto::NicInfo& proto) {
  if (!nic.IsValid()) return false;

  proto.set_name(nic.name);
  proto.set_type(static_cast<proto::NicInfo::Type>(nic.type));
  for (const auto& e : nic.endpoints) {
    auto* pe = proto.add_endpoints();
    if (!pe || !serialize(e, *pe)) return false;
  }
  return true;
}

bool Message::deserialize(const proto::NicInfo& proto, NicInfo& nic) {
  if (!proto.has_name()) return false;
  if (!proto.has_type()) return false;

  nic.name = proto.name();
  nic.type = static_cast<util::NicType>(proto.type());
  nic.endpoints.clear();
  nic.endpoints.reserve(proto.endpoints_size());
  Endpoint e;
  for (const auto& pe : proto.endpoints()) {
    if (!deserialize(pe, e)) return false;
    nic.endpoints.push_back(e);
  }
  return nic.IsValid();
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

}  // namespace peregrine::internal
