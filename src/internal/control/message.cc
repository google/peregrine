#include "src/internal/control/message.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

void Message::Convert(const Endpoint& e, absl::Span<const Request> requests,
                      proto::ReqMsg& msg) {
  DCHECK(e.IsValid());
  DCHECK(IsValid(requests));

  msg.Clear();
  proto::PeerRequests* pr = msg.mutable_peer_requests();
  pr->set_peer(e.ToString());

  proto::Request proto;
  for (const auto& r : requests) {
    auto* proto = pr->add_requests();
    DCHECK_NE(proto, nullptr);
    convert(r, *proto);
  }
}

std::pair<Endpoint, std::vector<Request>> Message::Convert(
    const proto::ReqMsg& msg) {
  DCHECK(msg.has_peer_requests());

  const auto invalid = std::make_pair(Endpoint(), std::vector<Request>(0));

  const proto::PeerRequests& pr = msg.peer_requests();
  const Endpoint e = Endpoint::Create(pr.peer());
  if (!e.IsValid()) {
    return invalid;
  }

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

  return {e, std::move(requests)};
}

void Message::convert(const Request& r, proto::Request& proto) {
  DCHECK(r.IsValid());
  DCHECK(r.op == Op::kRead || r.op == Op::kWrite);

  proto.set_op(r.op == Op::kRead ? proto::Request::READ
                                 : proto::Request::WRITE);
  proto.set_laddr(reinterpret_cast<uint64_t>(r.laddr));
  proto.set_raddr(reinterpret_cast<uint64_t>(r.raddr));
  proto.set_len(r.len);
}

bool Message::convert(const proto::Request& proto, Request& r) {
  const proto::Request::Op op = proto.op();
  if (op == proto::Request::INVALID) {
    return false;
  }

  DCHECK(op == proto::Request::READ || op == proto::Request::WRITE);
  r.op = op == proto::Request::READ ? Op::kRead : Op::kWrite;
  r.laddr = reinterpret_cast<Byte*>(proto.laddr());
  r.raddr = reinterpret_cast<Byte*>(proto.raddr());
  r.len = proto.len();
  return r.IsValid();
}

}  // namespace peregrine::internal
