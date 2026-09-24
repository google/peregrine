#ifndef PEREGRINE_SRC_API_SOCKET_UTIL_H_
#define PEREGRINE_SRC_API_SOCKET_UTIL_H_

#include <sys/uio.h>

#include <cstddef>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_util.h"
#include "src/internal/socket/tcp_socket_util.h"
#include "src/internal/util/util.h"

namespace peregrine {

// Writes exactly `len` bytes of data at the `buf` to the blocking socket `fd`.
// Returns OK if all the bytes are sent successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status WriteExact(int fd, const void* buf, size_t len) {
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));

  const Byte* const buffer = static_cast<const Byte*>(buf);
  return internal::TcpSocketUtil::Send(sock_fd, buffer, len);
}

// Writes all the bytes from the `iovs` to the blocking socket `fd`.
// Returns OK if all the bytes are sent successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status WriteVExact(int fd, absl::Span<const struct iovec> iovs) {
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));
  DCHECK(internal::IsValid(iovs));

  const size_t n = iovs.size();
  if ABSL_PREDICT_TRUE (1 <= n && n <= IOV_MAX) {
    return internal::TcpSocketUtil::SendV(sock_fd, iovs);
  }
  return absl::InvalidArgumentError(absl::StrCat("#iovs=", n));
}

// Reads exactly `len` bytes of data from the blocking socket `fd` to the `buf`.
// Returns OK if all the bytes are received successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status ReadExact(int fd, void* buf, size_t len) {
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));

  Byte* const buffer = static_cast<Byte*>(buf);
  return internal::TcpSocketUtil::Recv(sock_fd, buffer, len);
}

// Reads data from the blocking socket `fd` into the `iovs`.
// Returns OK if all the bytes are received successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status ReadVExact(int fd, absl::Span<const struct iovec> iovs) {
  const internal::fd_t sock_fd(fd);
  DCHECK(internal::IsValidSocket(sock_fd));
  DCHECK(internal::IsBlockingMode(sock_fd));
  DCHECK(internal::IsValid(iovs));

  const size_t n = iovs.size();
  if ABSL_PREDICT_TRUE (1 <= n && n <= IOV_MAX) {
    return internal::TcpSocketUtil::RecvV(sock_fd, iovs);
  }
  return absl::InvalidArgumentError(absl::StrCat("#iovs=", n));
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_SOCKET_UTIL_H_
