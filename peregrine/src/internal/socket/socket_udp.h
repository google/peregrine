#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/socket_base.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

// This class wraps a UDP/IPv{4,6} socket for unreliable network communications.
// This class is thread-compatible but not thread-safe.
class UdpSocket final : public SocketBase {
 public:
  // Creates an unconnected udp socket.
  static std::unique_ptr<UdpSocket> Create(int family, bool blocking);

  // Destructor closes the socket.
  ~UdpSocket();

  // Shuts down the socket for both send and recv.
  void Shutdown();

  // Binds to the `local` endpoint. Returns 0 on success, -1 on error.
  int Bind(const Endpoint& local) const;

  // Connects to the `peer` endpoint. Returns 0 on success, -1 on error.
  int Connect(const Endpoint& peer);

  // Sends the data remaining in `iovs` as a single datagram, advancing `iovs`
  // by the bytes sent. Returns the number of sent bytes (>= 0, either all the
  // remaining bytes or zero) on success, or -1 on error.
  ssize_t Send(IoVecCursor& iovs) const;

  // Receives a single datagram of at most `iovs.RemainingBytes()` bytes into
  // `iovs`, advancing it by the bytes read. Returns the number of received
  // bytes (>= 0) on success, or -1 on error.
  ssize_t Recv(IoVecCursor& iovs) const;

  // Returns a self/peer address pair string of the socket.
  std::string ToString() const;

 private:
  // Constructor with a valid file descriptor `fd`.
  // The `fd` comes from a successful `Create()` call.
  UdpSocket(fd_t fd, int family, bool blocking)
      : SocketBase(fd, family, blocking, /*connected=*/false) {
    DCHECK(invariant());
  }

 private:
  // Returns a success message for the last socket operation.
  static std::string okMsg(std::string_view func, fd_t fd) {
    return SuccessMsg(kUdp, func, fd);
  }

  // Returns a success message for the last socket operation.
  std::string okMsg(std::string_view func) const {
    return SuccessMsg(kUdp, func, fd_);
  }

  // Returns a success message for the socket send/recv call.
  std::string ioMsg(std::string_view func, size_t bytes) const {
    return SuccessMsg(kUdp, func, fd_, bytes);
  }

  // Returns an error message for the last socket operation.
  std::string errMsg(std::string_view func, util::Errno err) const {
    return ErrorMsg(kUdp, func, fd_, err);
  }

  static constexpr std::string_view kUdp = "udp";
};

inline std::ostream& operator<<(std::ostream& os, const UdpSocket& s) {
  return os << s.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
