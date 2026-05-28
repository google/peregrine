#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_

#include <sys/socket.h>
#include <sys/types.h>

#include "absl/log/check.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

// This class provides common functionalities for TCP/UDP sockets.
// It is not intended to be instantiated directly.
// Instead, instantiate the derived classes `TcpSocket` or `UdpSocket`.
//
// It is thread-compatible but not thread-safe.
class SocketBase {
 public:
  // Returns AF_INET for IPv4 and AF_INET6 for IPv6.
  int family() const { return family_; }

  // Returns the socket file descriptor.
  int fd() const { return fd_; }

  // Returns true iff the socket is up and running.
  bool IsValid() const { return fd_ >= 0; }

  // Returns true iff the socket is connected.
  bool IsConnected() const { return connected_; }

  // Returns true iff the socket is in blocking mode.
  bool IsBlocking() const { return IsValid() && IsBlockingMode(fd_); }

  // Returns true iff the socket is in non-blocking mode.
  bool IsNonBlocking() const { return IsValid() && IsNonBlockingMode(fd_); }

 protected:
  // Constructor.
  SocketBase(int fd, int family, bool connected)
      : fd_(fd), family_(family), connected_(connected) {
    DCHECK(invariant());
  }

  // Disallows copy since the socket owns OS resource.
  SocketBase(const SocketBase&) = delete;
  SocketBase& operator=(const SocketBase&) = delete;

  // Move constructor.
  SocketBase(SocketBase&& o) noexcept
      : fd_(o.fd_), family_(o.family_), connected_(o.connected_) {
    DCHECK(o.invariant());
    o.fd_ = -1;
  }

  // Move assignment operator.
  SocketBase& operator=(SocketBase&& o) noexcept {
    DCHECK(o.invariant());
    if (this != &o) {
      fd_ = o.fd_;
      family_ = o.family_;
      connected_ = o.connected_;
      o.fd_ = -1;
    }
    return *this;
  }

  // Destructor.
  ~SocketBase() { fd_ = -1; }

  // Returns true iff the invariant holds.
  bool invariant() const {
    return fd_ >= 0 && (family_ == AF_INET || family_ == AF_INET6);
  }

 protected:
  int fd_;
  int family_;
  bool connected_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_
