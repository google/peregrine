#include "src/internal/channel/channel_udp.h"

#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/internal/base/types.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

ssize_t UdpChannel::WriteV(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(iovecs.size(), 1);
  DCHECK_LE(iovecs.size(), IOV_MAX);
  DCHECK_GE(TotalLength(iovecs), 1);

  if ABSL_PREDICT_FALSE (iovecs.size() == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Send(buf, len);
  } else {
    DCHECK_GE(iovecs.size(), 2);
    return socket_->SendV(iovecs);
  }
}

ssize_t UdpChannel::ReadV(absl::Span<IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(iovecs.size(), 1);
  DCHECK_LE(iovecs.size(), IOV_MAX);
  DCHECK_GE(TotalLength(iovecs), 1);

  if ABSL_PREDICT_FALSE (iovecs.size() == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Recv(buf, len);
  } else {
    DCHECK_GE(iovecs.size(), 2);
    return socket_->RecvV(iovecs);
  }
}

std::string UdpChannel::ToString() const {
  return absl::StrCat("UdpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
