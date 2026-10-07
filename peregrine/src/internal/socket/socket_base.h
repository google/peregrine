#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_

#include <sys/socket.h>
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class provides common functionalities for TCP/UDP sockets.
// It is not intended to be instantiated directly.
// Instead, use the derived classes `TcpSocket` or `UdpSocket`.
// This class is thread-compatible but not thread-safe.
class SocketBase {
 public:
  // Returns the socket file descriptor.
  fd_t fd() const { return fd_; }

  // Returns AF_INET for IPv4 and AF_INET6 for IPv6.
  int family() const { return family_; }

  // Returns true iff the socket is in the specified family.
  bool MatchesFamily(int family) const {
    return std::cmp_equal(family, family_);
  }

  // Returns true iff the socket is up and running.
  bool IsValid() const { return IsValidSocket(fd_); }

  // Sets the connected state to true.
  void SetConnected() { connected_ = true; }

  // Returns true iff the socket is connected.
  bool IsConnected() const { return connected_; }

  // Sets the blocking mode to the specified value.
  // Returns 0 on success, -1 on error.
  [[nodiscard("Must check if the call succeeded")]]
  int SetBlocking(bool blocking);

  // Returns true iff the socket is in blocking mode.
  bool IsBlocking() const {
    DCHECK(MatchesBlocking());
    return blocking_;
  }

  // Returns true iff the socket is in non-blocking mode.
  bool IsNonBlocking() const {
    DCHECK(MatchesBlocking());
    return !blocking_;
  }

  // Returns true iff the socket is in the specified blocking mode.
  bool MatchesBlocking() const {
    return blocking_ ? IsBlockingMode(fd_) : IsNonBlockingMode(fd_);
  }

  // Returns a string representation of the socket.
  std::string ToString() const {
    DCHECK(invariant());
    return absl::StrFormat("ipv%d socket fd=%d %s (blocking=%d, connected=%d)",
                           family_ == AF_INET ? 4 : 6, fd_.value(),
                           AddrPortPair(fd_), blocking_ ? 1 : 0,
                           connected_ ? 1 : 0);
  }

 protected:
  // Constructor.
  SocketBase(fd_t fd, int family, bool blocking, bool connected)
      : fd_(fd), family_(family), blocking_(blocking), connected_(connected) {
    DCHECK_GE(fd.value(), 0);
    DCHECK(family == AF_INET || family == AF_INET6);
    DCHECK(invariant());
  }

  // Disallows copy since the socket owns OS resource.
  DISALLOW_COPY(SocketBase);

  // Disallows move to force the use of `std::unique_ptr<T>`, not `T`.
  DISALLOW_MOVE(SocketBase);

  // Destructor.
  ~SocketBase() { DCHECK_LT(fd_.value(), 0); }

  // Returns true iff the invariant holds.
  bool invariant() const {
    return fd_.value() >= 0 && IsValidSocket(fd_) &&
           (family_ == AF_INET || family_ == AF_INET6) &&
           AddrFamily(fd_) == family_ && MatchesBlocking();
  }

 protected:
  // Binds to the `local` endpoint. Returns 0 on success, -1 on error.
  static int Bind(fd_t fd, const Endpoint& local);

  // Connects to the `peer` endpoint. Returns 0 on success, -1 on error.
  static int Connect(fd_t fd, const Endpoint& peer);

 protected:
  static_assert(std::is_same_v<sa_family_t, uint16_t>);
  fd_t fd_;
  sa_family_t family_;
  bool blocking_;
  bool connected_;
};

static_assert(sizeof(SocketBase) == 8);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_BASE_H_
