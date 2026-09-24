#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_

#include <atomic>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This class listens on a local endpoint, accepts incoming connections, and
// creates a new tcp socket for each connection. It can also create a tcp
// socket and connects it to a peer endpoint which has a tcp listening socket.
// It is thread-compatible but not thread-safe.
class TcpManager {
  using OnAccept = absl::AnyInvocable<void(std::unique_ptr<TcpSocket>)>;

 public:
  using PspTokenExchange = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const PspToken& self_token, const Endpoint& peer,
      const Endpoint& peer_control)>;

  // Creates a tcp manager with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners` with the listening endpoints.
  static std::unique_ptr<TcpManager> Create(HostInfo& self);

  // Connects the `self` endpoint to the `peer` in blocking mode.
  // If `self` has nonzero ip address, binds to it before connecting.
  // Returns a connected tcp socket if successful, or null pointer otherwise.
  static std::unique_ptr<TcpSocket> Connect(const Endpoint& self,
                                            const Endpoint& peer);

  // Connects the `self` endpoint to the `peer` in blocking mode, while
  // exchanging PSP tokens with the `peer_control` endpoint. If `self` has
  // nonzero ip address, binds to it before connecting. Returns a connected
  // psp tcp socket if successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> ConnectPsp(
      const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
      PspTokenExchange& psp_token_xchg);

  // Starts running the manager.
  void Start(OnAccept on_accept, bool gen_blocking);

  // Stops the manager.
  void Stop();

  // Handles peer psp token exchange request for the `self_target` endpoint.
  absl::StatusOr<PspToken> ExchangePspTokens(const PspToken& peer_token,
                                             const Endpoint& self_target);

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
  TcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
             absl::flat_hash_map<fd_t, Listener> sockets)
      : self_(self),
        stop_(false),
        poller_(std::move(poller)),
        listeners_(std::move(sockets)) {
    DCHECK(invariant());
  }

  // Creates a tcp non-blocking socket listening on the `endpoint`.
  // If `endpoint.port` is 0, an ephemeral port will be used and filled back in.
  static std::unique_ptr<TcpSocket> createOne(Endpoint& endpoint,
                                              Poller& poller);

  // Returns true iff the stop flag is true.
  bool isStopped() const { return stop_.load(std::memory_order_relaxed); }

  // Returns true iff it is in a valid state.
  bool invariant() const;

 private:
  const HostInfo self_;
  std::atomic<bool> stop_;
  std::unique_ptr<Poller> poller_;
  const absl::flat_hash_map<fd_t, Listener> listeners_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
