#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
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

// This class is the same as `TcpManager`, but with PSP support.
// It is thread-compatible but not thread-safe.
// TODO(yongx): after `TcpManager` matures, consolidate dup code here.
class PspTcpManager {
  using OnAccept = absl::AnyInvocable<void(std::unique_ptr<TcpSocket>)>;

 public:
  using PspTokenExchange = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const PspToken& self_token, const Endpoint& peer,
      const Endpoint& peer_control)>;

  // Creates a tcp manager with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners` with the listening endpoints.
  static std::unique_ptr<PspTcpManager> Create(HostInfo& self);

  // Returns all the connected sockets.
  std::vector<std::unique_ptr<TcpSocket>> GetConnected()
      ABSL_LOCKS_EXCLUDED(connected_mu_) {
    absl::MutexLock _(connected_mu_);
    DCHECK(std::all_of(connected_.begin(), connected_.end(),
                       [](const auto& socket) { return socket != nullptr; }));
    return std::move(connected_);
  }

  // Connects the `self` endpoint to the `peer` in blocking/non-blocking mode.
  // If `self` has nonzero ip address, binds to it before connecting.
  // Returns 0 if successful, or -1 on error.
  int Connect(const Endpoint& self, const Endpoint& peer, bool blocking)
      ABSL_LOCKS_EXCLUDED(connected_mu_);

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

 private:
  // Constructor with a set of non-blocking tcp listening sockets.
  PspTcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
                absl::flat_hash_map<fd_t, Listener> sockets)
      : self_(self),
        stop_(false),
        poller_(std::move(poller)),
        listeners_(std::move(sockets)) {
    DCHECK(invariant());
  }

  // Adds a connected socket.
  void AddConnected(std::unique_ptr<TcpSocket> socket)
      ABSL_LOCKS_EXCLUDED(connected_mu_) {
    DCHECK_NE(socket, nullptr);
    absl::MutexLock _(connected_mu_);
    connected_.emplace_back(std::move(socket));
  }

  // Creates a tcp non-blocking socket listening on the `endpoint`.
  // If `endpoint.port` is 0, an ephemeral port will be used and filled back in.
  static std::unique_ptr<TcpSocket> createListener(Endpoint& endpoint,
                                                   Poller& poller);

  // Finds the listener matching the `target` endpoint.
  const Listener* findListener(const Endpoint& target) const;

  // Returns true iff the stop flag is true.
  bool isStopped() const { return stop_.load(std::memory_order_relaxed); }

  // Returns true iff it is in a valid state.
  bool invariant() const;

 private:
  const HostInfo self_;
  std::atomic<bool> stop_;

  absl::Mutex connected_mu_;
  std::vector<std::unique_ptr<TcpSocket>> connected_
      ABSL_GUARDED_BY(connected_mu_);

  std::unique_ptr<Poller> poller_;
  const absl::flat_hash_map<fd_t, Listener> listeners_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
