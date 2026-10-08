#ifndef PEREGRINE_SRC_API_SOCKET_UTIL_H_
#define PEREGRINE_SRC_API_SOCKET_UTIL_H_

#include <sys/uio.h>

#include <cstddef>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/internal/socket/tcp_socket_util.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine {

// Writes all the bytes from the `iovs` to the blocking socket `fd`.
// Returns OK if all the bytes are sent successfully by the `deadline`.
// Returns error otherwise, which means this socket can't be used anymore.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status WriteVExact(int fd, absl::Span<const struct iovec> iovs,
                                absl::Time deadline = absl::InfiniteFuture()) {
  if ABSL_PREDICT_FALSE (internal::TotalLength(iovs) == 0) {
    return absl::OkStatus();
  }
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));
  DCHECK(internal::IsValid(iovs));
  const size_t n = iovs.size();
  if ABSL_PREDICT_TRUE (1 <= n && n <= IOV_MAX) {
    return internal::TcpSocketUtil::Send(sock_fd, iovs, deadline);
  }
  return absl::InvalidArgumentError(absl::StrCat("#iovs=", n));
}

// Writes exactly `len` bytes of data at the `buf` to the blocking socket `fd`.
// Returns OK if all the bytes are sent successfully by the `deadline`.
// Returns error otherwise, which means this socket can't be used anymore.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status WriteExact(int fd, const void* buf, size_t len,
                               absl::Time deadline = absl::InfiniteFuture()) {
  return WriteVExact(fd, {{const_cast<void*>(buf), len}}, deadline);
}

// Reads data from the blocking socket `fd` into the `iovs`.
// Returns OK if all the bytes are received successfully by the `deadline`.
// Returns error otherwise, which means this socket can't be used anymore.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status ReadVExact(int fd, absl::Span<const struct iovec> iovs,
                               absl::Time deadline = absl::InfiniteFuture()) {
  if ABSL_PREDICT_FALSE (internal::TotalLength(iovs) == 0) {
    return absl::OkStatus();
  }
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));
  DCHECK(internal::IsValid(iovs));
  const size_t n = iovs.size();
  if ABSL_PREDICT_TRUE (1 <= n && n <= IOV_MAX) {
    return internal::TcpSocketUtil::Recv(sock_fd, iovs, deadline);
  }
  return absl::InvalidArgumentError(absl::StrCat("#iovs=", n));
}

// Reads exactly `len` bytes of data from the blocking socket `fd` to the `buf`.
// Returns OK if all the bytes are received successfully by the `deadline`.
// Returns error otherwise, which means this socket can't be used anymore.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status ReadExact(int fd, void* buf, size_t len,
                              absl::Time deadline = absl::InfiniteFuture()) {
  return ReadVExact(fd, {{buf, len}}, deadline);
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_SOCKET_UTIL_H_
