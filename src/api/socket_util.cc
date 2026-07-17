#include "src/api/socket_util.h"

#include <sys/uio.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace peregrine {

absl::Status WriteVExact(int fd, absl::Span<const struct iovec> iovs) {
  for (const auto& iov : iovs) {
    const absl::Status s = WriteExact(fd, iov.iov_base, iov.iov_len);
    if (!s.ok()) return s;
  }
  return absl::OkStatus();
}

absl::Status ReadVExact(int fd, absl::Span<const struct iovec> iovs) {
  for (const auto& iov : iovs) {
    const absl::Status s = ReadExact(fd, iov.iov_base, iov.iov_len);
    if (!s.ok()) return s;
  }
  return absl::OkStatus();
}

}  // namespace peregrine
