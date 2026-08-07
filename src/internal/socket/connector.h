#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_

#include <memory>

#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This is a utility class that creates a tcp socket and connects it to
// a peer endpoint which has a tcp acceptor socket listening.
// It is thread-safe since it has no state.
class TcpConnector {
 public:
  // Connects to the `peer` endpoint. If `local` has nonzero ip address,
  // binds to it before connecting. Returns a connected tcp socket if
  // successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> Create(const Endpoint& peer,
                                           const Endpoint& local = {});
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
