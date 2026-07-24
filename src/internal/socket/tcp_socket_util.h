#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_

// NOTE: Do __not__ modify this file.
// It is temporarily used by tpu raiden.
// It will soon be replaced by the `TcpSocket` class.

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

// This utility class implements tcp socket send/recv functions.
// This class is thread-safe since it has no data members.
class TcpSocketUtil final {
 public:
  // Sends on the socket `fd` exactly `len` bytes of data from the `buf`.
  // Returns OK if all the bytes are sent successfully, error otherwise.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status Send(fd_t fd, const Byte* buf, size_t len);

  // Receives on the socket `fd` exactly `len` bytes of data into the `buf`.
  // Returns OK if all the bytes are received successfully, error otherwise.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status Recv(fd_t fd, Byte* buf, size_t len);

  // Sends on the socket `fd` exactly all the data from the `iovecs` buffers.
  // Returns OK if all the bytes are sent successfully, error otherwise.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status SendV(fd_t fd, absl::Span<const IoVec> iovecs);

  // Receives on the socket `fd` exactly all the data into the `iovecs` buffers.
  // Returns OK if all the bytes are received successfully, error otherwise.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status RecvV(fd_t fd, absl::Span<const IoVec> iovecs);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_
