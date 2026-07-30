#include "src/internal/channel/channel_udp.h"

#include <cstddef>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/internal/base/types.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

bool UdpChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_GE(iovecs.size(), 1);
  if (iovecs.size() == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Send(buf, len) == len;
  } else {
    DCHECK_GE(iovecs.size(), 2);
    return socket_->SendV(iovecs) == len;
  }
}

std::string UdpChannel::ToString() const {
  return absl::StrCat("UdpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
