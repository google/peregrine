#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_

#include <memory>

#include "absl/status/statusor.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/psp/tcp_psp_helper.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This is a utility class that creates a tcp socket and connects it to
// a peer endpoint which has a tcp acceptor socket listening.
// It is thread-safe since it has no state.
class TcpConnector {
 public:
  // Creates an unconnected tcp socket suitable for connecting to `peer`.
  // If `local` has nonzero ip address, binds to it. Returns the socket if
  // successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> CreateUnconnected(
      const Endpoint& peer, const Endpoint& local = {});

  // Allocates a fresh RX SPI and key on the given `socket` for PSP encryption.
  static absl::StatusOr<PspSpiKey> AcquireRxSpiAndKey(const TcpSocket& socket);

  // Connects the `socket` to the `peer` endpoint. Returns true if successful.
  static bool Connect(TcpSocket& socket, const Endpoint& peer);

  // Connects the `socket` to the `peer` endpoint with PSP encryption.
  // Configures PSP encryption using `server_key` and verifies negotiated SPI
  // against `client_key` on connection. Returns true if successful.
  static bool PspConnect(TcpSocket& socket, const Endpoint& peer,
                         const PspSpiKey& server_key,
                         const PspSpiKey& client_key);

  // Connects to the `peer` endpoint. If `local` has nonzero ip address,
  // binds to it before connecting. Returns a connected tcp socket if
  // successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> Create(const Endpoint& peer,
                                           const Endpoint& local = {});
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_CONNECTOR_H_
