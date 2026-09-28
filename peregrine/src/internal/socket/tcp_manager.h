#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager_base.h"

namespace peregrine::internal {

// This class manages the creation of tcp sockets, either passively listening
// on a local endpoint and accepting incoming connections, or actively
// connecting to a peer endpoint.
// It is thread-safe.
class TcpManager : public TcpManagerBase {
  using OnAccept = absl::AnyInvocable<void(std::unique_ptr<TcpSocket>)>;

 public:
  // Creates a tcp manager with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners` with the listening endpoints.
  static std::unique_ptr<TcpManager> Create(HostInfo& self);

  // Returns all the outgoing connected sockets.
  std::vector<std::unique_ptr<TcpSocket>> GetConnected() {
    return outgoing_.MoveAll();
  }

  // Connects the `self` endpoint to the `peer` in blocking/non-blocking mode.
  // If `self` has nonzero ip address, binds to it before connecting.
  // Returns 0 if successful, or -1 on error.
  int Connect(const Endpoint& self, const Endpoint& peer, bool blocking);

  // Starts running the manager.
  void Start(OnAccept on_accept, bool gen_blocking);

  // Stops the manager.
  void Stop();

 private:
  // Constructor with a set of non-blocking tcp listening sockets.
  TcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
             absl::node_hash_map<fd_t, Listener> sockets);

  // Returns true iff the stop flag is true.
  bool isStopped() const { return stop_.load(std::memory_order_relaxed); }

  // Returns true iff it is in a valid state.
  bool invariant() const;

 private:
  // Creates a non-blocking tcp listening socket on the `endpoint`.
  // If `endpoint.port` is 0, an ephemeral port will be used and updates it.
  static std::unique_ptr<TcpSocket> createListener(Endpoint& endpoint,
                                                   Poller& poller);

  // Unregisters and removes the listening socket `fd`.
  void removeListener(fd_t fd);

  // Handles all incoming connections on the listening socket `fd`.
  bool handleAllIncoming(OnAccept& on_accept, bool gen_blocking, fd_t fd,
                         uint32_t flag);

 private:
  const HostInfo self_;

  std::atomic<bool> stop_;
  std::unique_ptr<Poller> poller_;

  Listeners listeners_;
  Produced outgoing_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
