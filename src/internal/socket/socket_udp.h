#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_base.h"

namespace peregrine {

// This class wraps a UDP/IPv{4,6} socket for unreliable network communications.
// It is movable but not copyable.
//
// This class is thread-compatible but not thread-safe.
class UdpSocket final : public SocketBase {
 public:
  // Creates an unconnected udp socket.
  static absl::StatusOr<std::unique_ptr<UdpSocket>> Create(int family);

  // Destructor closes the socket.
  ~UdpSocket();

  // Binds to the local `ip:port`.
  absl::Status Bind(const IpAddr& ip, port_t port) const;

  // Connects to the peer `ip:port`.
  absl::Status Connect(const IpAddr& ip, port_t port);

  // Sends `len` bytes of data from the `buf`. Returns OK if all the data has
  // been sent successfully. Otherwise, returns an error status.
  absl::Status Send(const Byte* buf, size_t len) const;

  // Receives at most `len` bytes of data into the `buf`. Returns the number of
  // bytes received if successful. Otherwise, returns an error status.
  absl::StatusOr<size_t> Recv(Byte* buf, size_t len) const;

  // Sends `len` bytes of data from `n` `iov` buffers. Returns OK if all the
  // data has been sent successfully. Otherwise, returns an error status.
  absl::Status SendV(const IoVec* iov, int n, size_t len) const;

  // Receives at most `len` bytes of data into `n` `iov` buffers. Returns the
  // number of bytes received if successful. Otherwise, returns an error status.
  absl::StatusOr<size_t> RecvV(const IoVec* iov, int n, size_t len) const;

  // Returns a self/peer address pair string of the socket.
  std::string ToString() const;

 private:
  // Constructor with a valid file descriptor `fd`.
  // The `fd` comes from a successful `Create()` call.
  UdpSocket(int fd, int family) : SocketBase(fd, family, /*connected=*/false) {}
};

inline std::ostream& operator<<(std::ostream& os, const UdpSocket& s) {
  return os << s.ToString();
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UDP_H_
