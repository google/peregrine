#include "src/internal/channel/channel_udp.h"

#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/internal/base/types.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

ssize_t UdpChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));

  DCHECK_GE(TotalLength(iovecs), 1);
  DCHECK_GE(iovecs.size(), 1);
  if (iovecs.size() == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Send(buf, len);
  } else {
    DCHECK_GE(iovecs.size(), 2);
    return socket_->SendV(iovecs);
  }
}

std::string UdpChannel::ToString() const {
  return absl::StrCat("UdpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
