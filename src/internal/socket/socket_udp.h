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

#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_base.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

// This class wraps a UDP/IPv{4,6} socket for unreliable network communications.
// It is movable but not copyable.
//
// This class is thread-compatible but not thread-safe.
class UdpSocket final : public SocketBase {
 public:
  // Creates an unconnected udp socket.
  static std::unique_ptr<UdpSocket> Create(int family);

  // Destructor closes the socket.
  ~UdpSocket();

  // Binds to the local `ip:port`.
  bool Bind(const IpAddr& ip, port_t port) const;

  // Connects to the peer `ip:port`.
  bool Connect(const IpAddr& ip, port_t port);

  // Sends `len` bytes of data from the `buf`. Returns true if all the data has
  // been sent successfully. Otherwise, returns false.
  bool Send(const Byte* buf, size_t len) const;

  // Receives at most `len` bytes of data into the `buf`. Returns the number of
  // bytes received if successful. Otherwise, returns -1.
  ssize_t Recv(Byte* buf, size_t len) const;

  // Sends `len` bytes of data from `n` `iov` buffers. Returns true if all the
  // data has been sent successfully. Otherwise, returns false.
  bool SendV(const IoVec* iov, int n, size_t len) const;

  // Receives at most `len` bytes of data into `n` `iov` buffers. Returns the
  // number of bytes received if successful. Otherwise, returns -1.
  ssize_t RecvV(const IoVec* iov, int n, size_t len) const;

  // Returns a self/peer address pair string of the socket.
  std::string ToString() const;

 private:
  // Constructor with a valid file descriptor `fd`.
  // The `fd` comes from a successful `Create()` call.
  UdpSocket(int fd, int family) : SocketBase(fd, family, /*connected=*/false) {}

  // Returns a success message for the last socket operation.
  static std::string successMsg(std::string_view func, int fd) {
    return SuccessMsg("udp", func, fd);
  }

  // Returns a success message for the last socket operation.
  std::string successMsg(std::string_view func) const {
    return SuccessMsg("udp", func, fd_);
  }

  // Returns an error message for the last socket operation.
  std::string errorMsg(std::string_view func) const {
    return ErrorMsg("udp", func, fd_);
  }
};

inline std::ostream& operator<<(std::ostream& os, const UdpSocket& s) {
  return os << s.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
