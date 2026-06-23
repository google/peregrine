#include "src/internal/channel/channel_tcp.h"

#include <cstddef>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

bool TcpChannel::Write(absl::Span<const IoVec> iovecs) {
  DCHECK(!iovecs.empty());

  for (const IoVec& v : iovecs) {
    const Byte* const buf = reinterpret_cast<const Byte*>(v.iov_base);
    const size_t len = v.iov_len;
    if (socket_->Send(buf, len) != len) return false;
  }
  return true;
}

std::string TcpChannel::ToString() const {
  return absl::StrCat("TcpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
