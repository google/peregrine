#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_H_

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

// This class wraps a TCP/IPv{4,6} socket for reliable network communications.
// It is movable but not copyable.
//
// This class is thread-compatible but not thread-safe.
class TcpSocket final : public SocketBase {
 public:
  // Creates an unconnected tcp socket.
  static std::unique_ptr<TcpSocket> Create(int family);

  // Creates a connected tcp socket.
  static std::unique_ptr<TcpSocket> Create(int fd, int family);

  // Destructor closes the socket.
  ~TcpSocket();

  // Listens on the local `ip:port`.
  bool Listen(const IpAddr& ip, port_t port) const;

  // Accepts a new connection by this listening socket. Returns the new spawn
  // socket file descriptor if successful. Otherwise, returns -1.
  int Accept() const;

  // Connects to the peer `ip:port`.
  bool Connect(const IpAddr& ip, port_t port);

  // Sends `len` bytes of data from the `buf`. Returns true if all the data has
  // been sent successfully. Otherwise, returns false.
  bool Send(const Byte* buf, size_t len) const;

  // Sends `len` bytes of data from `n` `iov` buffers. Returns true if all the
  // data has been sent successfully. Otherwise, returns false.
  // bool Send(const struct iovec* iov, int n, size_t len) const;

  // Receives exactly `len` bytes of data into the `buf`.
  // Returns true if successful. Otherwise, returns false.
  bool Recv(Byte* buf, size_t len) const;

  // Returns a self/peer address pair string of the socket.
  std::string ToString() const;

 private:
  // Constructor with a valid file descriptor `fd`.
  // The `fd` comes from a successful `Create()` or `Accept()` call.
  TcpSocket(int fd, int family, bool connected)
      : SocketBase(fd, family, connected) {}

  // Returns a success message for the last socket operation.
  static std::string successMsg(std::string_view func, int fd) {
    return SuccessMsg("tcp", func, fd);
  }

  // Returns a success message for the last socket operation.
  std::string successMsg(std::string_view func) const {
    return SuccessMsg("tcp", func, fd_);
  }

  // Returns an error message for the last socket operation.
  std::string errorMsg(std::string_view func) const {
    return ErrorMsg("tcp", func, fd_);
  }
};

inline std::ostream& operator<<(std::ostream& os, const TcpSocket& s) {
  return os << s.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_H_
