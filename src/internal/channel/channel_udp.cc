#include "src/internal/channel/channel_udp.h"

#include <cstddef>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

bool UdpChannel::Write(absl::Span<const IoVec> iovecs) {
  const int n = iovecs.size();
  DCHECK_GE(n, 1);
  if ABSL_PREDICT_FALSE (n == 1) {
    const IoVec& v = iovecs.front();
    const Byte* const buf = reinterpret_cast<const Byte*>(v.iov_base);
    const size_t len = v.iov_len;
    return socket_->Send(buf, len);
  } else {
    const IoVec* iov = iovecs.data();
    const size_t len = TotalLength(iov, n);
    return socket_->SendV(iov, n, len);
  }
}

std::string UdpChannel::ToString() const {
  return absl::StrCat("UdpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
