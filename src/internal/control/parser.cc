#include "src/internal/control/parser.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "src/api/transport_types.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

std::string ControlMsg::serialize(const Request& r) {
  proto::Request proto;
  proto.set_op(static_cast<proto::Request::Op>(r.op));
  proto.set_laddr(reinterpret_cast<uint64_t>(r.laddr));
  proto.set_raddr(reinterpret_cast<uint64_t>(r.raddr));
  proto.set_len(r.len);
  return proto.SerializeAsString();
}

bool ControlMsg::deserialize(const std::string_view s, Request& r) {
  proto::Request proto;
  if (!proto.ParseFromString(s)) {
    return false;
  }
  if (proto.op() == proto::Request::INVALID) {
    return false;
  }
  r.op = static_cast<Op>(proto.op());
  r.laddr = reinterpret_cast<Byte*>(proto.laddr());
  r.raddr = reinterpret_cast<Byte*>(proto.raddr());
  r.len = proto.len();
  return true;
}

}  // namespace peregrine::internal
