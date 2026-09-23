#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This is a utility class that creates a tcp socket and connects it to
// a peer endpoint which has a tcp acceptor socket listening.
// It is thread-safe since it has no state.
class TcpConnector {
 public:
  using PspTokenExchangeFunc = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const PspToken&, const Endpoint&, const Endpoint&)>;

  // Connects the `self` endpoint to the `peer` in blocking mode.
  // If `self` has nonzero ip address, binds to it before connecting.
  // Returns a connected tcp socket if successful, or null pointer otherwise.
  static std::unique_ptr<TcpSocket> Create(const Endpoint& self,
                                           const Endpoint& peer);

  // Connects the `self` endpoint to the `peer` in blocking mode, while
  // exchanging PSP tokens with the `peer_control` endpoint. If `self` has
  // nonzero ip address, binds to it before connecting. Returns a connected
  // psp tcp socket if successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> CreatePsp(
      const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
      PspTokenExchangeFunc& psp_exchange_func);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
