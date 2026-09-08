#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_

#include <atomic>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/event/poller.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This class listens on a local endpoint, accepts incoming connections, and
// creates a new tcp socket for each connection.
// It is thread-compatible but not thread-safe.
class TcpAcceptor {
  using AcceptCallback = absl::AnyInvocable<void(std::unique_ptr<TcpSocket>)>;

 public:
  // Creates a tcp acceptor with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners` with the listening endpoints.
  static std::unique_ptr<TcpAcceptor> Create(HostInfo& self);

  // Returns the listening endpoints of the acceptor.
  absl::Span<const Endpoint> Listeners() const {
    DCHECK(self_.IsValid());
    return self_.data_plane_listeners;
  }

  // Starts running the acceptor.
  void Start(AcceptCallback accept);

  // Handles peer psp token exchange request for the `self_target` endpoint.
  absl::StatusOr<PspToken> ExchangePspTokens(const PspToken& peer_token,
                                             const Endpoint& self_target);

  // Stops the acceptor.
  void Stop();

 private:
  struct Listener {
    std::unique_ptr<TcpSocket> socket;
    Endpoint endpoint;  // cache for SelfAddrPort(socket->fd())
    mutable std::unique_ptr<absl::Mutex> mu;  // for psp token exchange

    Listener(std::unique_ptr<TcpSocket> s, const Endpoint& e)
        : socket(std::move(s)),
          endpoint(e),
          mu(std::make_unique<absl::Mutex>()) {}
  };

  // Finds the listener matching the `target` endpoint.
  const Listener* findListener(const Endpoint& target) const;

 private:
  // Constructor with a set of non-blocking tcp listening sockets.
  TcpAcceptor(const HostInfo& self, std::unique_ptr<Poller> poller,
              absl::flat_hash_map<fd_t, Listener> sockets)
      : self_(self),
        stop_(false),
        poller_(std::move(poller)),
        listeners_(std::move(sockets)) {
    DCHECK(invariant());
  }

  // Creates a tcp non-blocking socket listening on the `endpoint`.
  // If `endpoint.port` is 0, an ephemeral port will be used and
  // fills back in the `endpoint.port`.
  static std::unique_ptr<TcpSocket> createOne(Endpoint& endpoint,
                                              Poller* poller);

  // Returns true iff the acceptor is in a valid state.
  bool invariant() const;

 private:
  const HostInfo self_;
  std::atomic<bool> stop_;
  std::unique_ptr<Poller> poller_;
  const absl::flat_hash_map<fd_t, Listener> listeners_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_
