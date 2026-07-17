#ifndef PEREGRINE_SRC_API_SOCKET_UTIL_H_
#define PEREGRINE_SRC_API_SOCKET_UTIL_H_

#include <sys/uio.h>

#include <cstddef>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine {

// Writes exactly `len` bytes of data from the `buf` to the socket `fd`.
// Returns OK if all the bytes are sent successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status WriteExact(int fd, const void* buf, size_t len) {
  DCHECK_GE(fd, 0);
  DCHECK_NE(buf, nullptr);
  DCHECK_GE(len, 1);
  const Byte* const buffer = static_cast<const Byte*>(buf);
  return internal::TcpSocket::Send(internal::fd_t(fd), buffer, len);
}

// Writes all the bytes from the `iovs` to the socket `fd`.
// Returns OK if all the bytes are sent successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
absl::Status WriteVExact(int fd, absl::Span<const struct iovec> iovs);

// Reads exactly `len` bytes of data from the socket `fd` into the `buf`.
// Returns OK if all the bytes are received successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
inline absl::Status ReadExact(int fd, void* buf, size_t len) {
  DCHECK_GE(fd, 0);
  DCHECK_NE(buf, nullptr);
  DCHECK_GE(len, 1);
  Byte* const buffer = static_cast<Byte*>(buf);
  return internal::TcpSocket::Recv(internal::fd_t(fd), buffer, len);
}

// Reads from the socket `fd` into the `iovs`.
// Returns OK if all the bytes are received successfully, error otherwise.
// Precondition: the caller must ensure the input parameters are valid.
absl::Status ReadVExact(int fd, absl::Span<const struct iovec> iovs);

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_SOCKET_UTIL_H_
