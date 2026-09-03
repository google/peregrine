#include "src/internal/control/message.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"

namespace peregrine::internal {

bool Message::Convert(const HostInfo& host, absl::Span<const Request> requests,
                      proto::ReqMsg& msg) {
  DCHECK(host.IsValid());
  DCHECK(IsValid(requests));

  msg.Clear();

  proto::PeerRequests* pr = msg.mutable_peer_requests();
  auto* peer = pr->mutable_peer();
  if (peer == nullptr) return false;
  if (!convert(host, *peer)) return false;

  proto::Request proto;
  for (const auto& r : requests) {
    auto* proto = pr->add_requests();
    if (proto == nullptr) return false;
    if (!convert(r, *proto)) return false;
  }
  return true;
}

std::pair<HostInfo, std::vector<Request>> Message::Convert(
    const proto::ReqMsg& msg) {
  DCHECK(msg.has_peer_requests());

  const auto invalid = std::make_pair(HostInfo(), std::vector<Request>(0));

  HostInfo host;
  const proto::PeerRequests& pr = msg.peer_requests();
  if (!convert(pr.peer(), host)) return invalid;

  std::vector<Request> requests;
  requests.reserve(pr.requests_size());
  Request r;
  for (const auto& proto : pr.requests()) {
    if (!convert(proto, r)) return invalid;
    requests.push_back(r);
  }
  if (!IsValid(requests)) {
    return invalid;
  }

  return {host, std::move(requests)};
}

bool Message::Convert(const HostInfo& host, proto::ReqMsg& msg) {
  msg.Clear();
  return convert(host, *msg.mutable_host_info());
}

bool Message::Convert(const proto::ReqMsg& msg, HostInfo& host) {
  if (!msg.has_host_info()) return false;
  return convert(msg.host_info(), host);
}

bool Message::Convert(const HostInfo& host, proto::RespMsg& msg) {
  msg.Clear();
  return convert(host, *msg.mutable_host_info());
}

bool Message::Convert(const proto::RespMsg& msg, HostInfo& host) {
  if (!msg.has_host_info()) return false;
  return convert(msg.host_info(), host);
}

bool Message::convert(const HostInfo& host, proto::HostInfo& proto) {
  if (!host.IsValid()) return false;

  proto.mutable_control_plane_listener()->set_ip_port(
      host.control_plane_listener.ToString());

  for (const auto& e : host.data_plane_listeners) {
    auto* dp = proto.add_data_plane_listeners();
    if (dp == nullptr) return false;
    dp->set_ip_port(e.ToString());
  }

  for (const auto& r : host.rdma_interfaces) {
    auto* rdma = proto.add_rdma_interfaces();
    if (rdma == nullptr) return false;
    rdma->set_name(r.name);
    rdma->set_gid(r.gid);
    rdma->set_port_num(r.port_num);
  }
  return true;
}

bool Message::convert(const proto::HostInfo& proto, HostInfo& host) {
  const auto ip_port = proto.control_plane_listener().ip_port();
  const Endpoint c = Endpoint::Create(ip_port);
  if (!c.HasNonzeroIpPort()) {
    return false;
  }

  std::vector<Endpoint> ds;
  ds.reserve(proto.data_plane_listeners_size());
  for (const auto& l : proto.data_plane_listeners()) {
    const Endpoint e = Endpoint::Create(l.ip_port());
    if (!e.HasNonzeroIpPort()) return false;
    ds.push_back(e);
  }

  std::vector<RdmaInterface> rdma_ifs;
  rdma_ifs.reserve(proto.rdma_interfaces_size());
  for (const auto& r : proto.rdma_interfaces()) {
    if (r.name().empty() || r.gid().size() != 16) return false;
    rdma_ifs.push_back(RdmaInterface{
        .name = std::string(r.name()),
        .gid = std::string(r.gid()),
        .port_num = r.port_num() ? r.port_num() : 1,
    });
  }

  host = {
      .control_plane_listener = c,
      .data_plane_listeners = std::move(ds),
      .rdma_interfaces = std::move(rdma_ifs),
  };
  DCHECK(host.IsValid());
  return true;
}

bool Message::convert(const Request& r, proto::Request& proto) {
  if (!r.IsValid()) return false;

  DCHECK(r.op == Op::kRead || r.op == Op::kWrite);
  proto.set_op(r.op == Op::kRead ? proto::Request::READ
                                 : proto::Request::WRITE);
  proto.set_laddr(reinterpret_cast<uint64_t>(r.laddr));
  proto.set_raddr(reinterpret_cast<uint64_t>(r.raddr));
  proto.set_len(r.len);
  proto.set_rkey(r.rkey);
  return true;
}

bool Message::convert(const proto::Request& proto, Request& r) {
  const proto::Request::Op op = proto.op();
  if (op == proto::Request::INVALID) return false;

  DCHECK(op == proto::Request::READ || op == proto::Request::WRITE);
  r.op = op == proto::Request::READ ? Op::kRead : Op::kWrite;
  r.laddr = reinterpret_cast<Byte*>(proto.laddr());
  r.raddr = reinterpret_cast<Byte*>(proto.raddr());
  r.len = proto.len();
  r.rkey = proto.rkey();
  return r.IsValid();
}

}  // namespace peregrine::internal
