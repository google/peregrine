#ifndef PEREGRINE_SRC_API_V1_IP_UTIL_H_
#define PEREGRINE_SRC_API_V1_IP_UTIL_H_

#include <string>

#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"

namespace peregrine::v1 {

// Returns the local "ip:port" for the socket, or "?" if it can't be determined.
inline std::string GetLocalEndpoint(int sock_fd) {
  const internal::fd_t fd(sock_fd);
  return internal::SelfAddrPort(fd);
}

// Returns a self/peer address pair ("ip:port--ip:port") for the socket.
// Each side is "?" if it can't be determined; the peer is "*" if the socket
// is not connected.
inline std::string GetAddrPortPair(int sock_fd) {
  const internal::fd_t fd(sock_fd);
  return internal::AddrPortPair(fd);
}

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_IP_UTIL_H_
