#include "src/api/types.h"

#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"

namespace peregrine {

std::string ToString(const Op op) {
  switch (op) {
    case Op::kRead:
      return "Read";
    case Op::kWrite:
      return "Write";
    default:
      DCHECK(false) << "Unreachable";
  }
}

std::string Request::ToString() const {
  return absl::StrFormat(
      "Request(op: %s, local_addr: %p, remote_addr: %p, len: %d)",
      peregrine::ToString(op), laddr, raddr, len);
}

}  // namespace peregrine
