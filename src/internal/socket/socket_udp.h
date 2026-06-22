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
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_base.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

// This class wraps a UDP/IPv{4,6} socket for unreliable network communications.
// It is movable but not copyable.
// This class is thread-compatible but not thread-safe.
class UdpSocket final : public SocketBase {
 public:
  // Creates an unconnected udp socket.
  static std::unique_ptr<UdpSocket> Create(int family);

  // Destructor closes the socket.
  ~UdpSocket();

  // Binds to the `local` endpoint.
  bool Bind(const Endpoint& local) const;

  // Connects to the `peer` endpoint.
  bool Connect(const Endpoint& peer);

  // Sends `len` bytes of data from the `buf`. Returns true if all the data has
  // been sent successfully. Otherwise, returns false.
  bool Send(const Byte* buf, size_t len) const;

  // Receives at most `len` bytes of data into the `buf`.
  // Returns the number of bytes received if successful. Zero byte means the
  // received packet has no payload. Returns -1 on error.
  ssize_t Recv(Byte* buf, size_t len) const;

  // Sends `len` bytes of data from `n` `iov` buffers. Returns true if all the
  // data has been sent successfully. Otherwise, returns false.
  bool SendV(const IoVec* iov, int n, size_t len) const;

  // Receives at most `len` bytes of data into `n` `iov` buffers.
  // Returns the number of bytes received if successful. Zero byte means the
  // received packet has no payload. Returns -1 on error.
  ssize_t RecvV(const IoVec* iov, int n, size_t len) const;

  // Returns a self/peer address pair string of the socket.
  std::string ToString() const;

 private:
  // Constructor with a valid file descriptor `fd`.
  // The `fd` comes from a successful `Create()` call.
  UdpSocket(fd_t fd, int family) : SocketBase(fd, family, /*connected=*/false) {
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
  std::string errMsg(std::string_view func, int last_errno) const {
    return ErrorMsg(kUdp, func, fd_, last_errno);
  }

  static constexpr std::string_view kUdp = "udp";
};

inline std::ostream& operator<<(std::ostream& os, const UdpSocket& s) {
  return os << s.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
