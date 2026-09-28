#include "peregrine/src/internal/channel/channel_tcp.h"

#include <cstddef>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine::internal {

ssize_t TcpChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(TotalLength(iovecs), 1);

  const size_t n = iovecs.size();
  DCHECK(1 <= n && n <= IOV_MAX);
  if ABSL_PREDICT_FALSE (n == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Send(buf, len);
  } else {
    DCHECK_GE(n, 2);
    return socket_->SendV(iovecs);
  }
}

ssize_t TcpChannel::Read(absl::Span<IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(TotalLength(iovecs), 1);

  const size_t n = iovecs.size();
  DCHECK(1 <= n && n <= IOV_MAX);
  if ABSL_PREDICT_FALSE (n == 1) {
    const auto [buf, len] = BufLen(iovecs[0]);
    return socket_->Recv(buf, len);
  } else {
    DCHECK_GE(n, 2);
    return socket_->RecvV(iovecs);
  }
}

std::string TcpChannel::ToString() const {
  return absl::StrCat("TcpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
