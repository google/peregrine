#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_

// NOTE: Do __not__ modify this file.
// It is temporarily used by tpu raiden.
// It will soon be replaced by the `TcpSocket` class.

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal {

// This utility class implements blocking tcp socket send/recv functions.
// It will be deprecated once TPU Raiden code has been migrated to use the
// Peregrine API (see `src/api/transport.h`).
class TcpSocketUtil final {
 public:
  // Sends on the socket `fd` exactly all the data from the `iovecs` buffers.
  // Returns OK if all the bytes are sent successfully by the deadline set by
  // `timeout_ms`, which is infinite if `timeout_ms` < 0. Otherwise returns
  // error, which means this socket can't be used anymore.
  // Note: the deadline is only enforced while blocked waiting on the socket,
  // so a transfer that keeps making progress may complete after the deadline.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status Send(fd_t fd, absl::Span<const IoVec> iovecs,
                           int timeout_ms);

  // Receives on the socket `fd` exactly all the data into the `iovecs` buffers.
  // Returns OK if all the bytes are received successfully by the deadline set
  // by `timeout_ms`, which is infinite if `timeout_ms` < 0. Otherwise returns
  // error, which means this socket can't be used anymore.
  // Note: the deadline is only enforced while blocked waiting on the socket,
  // so a transfer that keeps making progress may complete after the deadline.
  ABSL_DEPRECATED("temporarily for tpu raiden")
  static absl::Status Recv(fd_t fd, absl::Span<const IoVec> iovecs,
                           int timeout_ms);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_SOCKET_UTIL_H_
