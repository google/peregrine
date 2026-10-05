#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
 public:
  // Creates a tcp manager with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners` with the listening endpoints.
  static std::unique_ptr<TcpManager> Create(HostInfo& self);

  // Starts running the manager, blocking until `Stop()` is called. It must be
  // called at most once. The caller thread must be joined before destroying
  // the manager.
  void Start(bool gen_blocking);

  // Stops the manager. No further `Connect()` succeeds, no new sockets will be
  // produced, and `Start()` returns promptly.
  void Stop();

  // Connects the `self` endpoint to the `peer` in blocking/non-blocking mode.
  // If `self` has nonzero ip address, binds to it before connecting.
  // Returns 0 on success, 1 on connect in progress, or -1 on error.
  int Connect(const Endpoint& self, const Endpoint& peer, bool blocking);

  // Returns all the accepted incoming sockets currently available.
  std::vector<std::unique_ptr<TcpSocket>> GetIncomingSockets() {
    return incoming_.MoveAll();
  }

  // Returns all the connected outgoing sockets currently available.
  std::vector<std::unique_ptr<TcpSocket>> GetOutgoingSockets() {
    return outgoing_.MoveAll();
  }

 private:
  // Constructor with a set of non-blocking tcp listening sockets.
  TcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
             absl::flat_hash_map<fd_t, Listener> sockets);

  // Returns true iff the stop flag is true.
  bool isStopped() const { return stop_.load(std::memory_order_acquire); }

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
  bool handleAllIncoming(fd_t fd, uint32_t flag, bool gen_blocking);

 private:
  // Creates a blocking tcp connecting socket to the `peer` endpoint.
  int connectBlocking(std::unique_ptr<TcpSocket> socket, const Endpoint& peer);

  // Creates a non-blocking tcp connecting socket to the `peer` endpoint.
  int connectNonBlocking(std::unique_ptr<TcpSocket> socket,
                         const Endpoint& peer);

  // Handles one outgoing connection on the connecting socket `fd`.
  bool handleOneOutgoing(fd_t fd, uint32_t flag);

  // Adds a connected socket to the outgoing sockets.
  int addConnected(std::unique_ptr<TcpSocket> socket);

 private:
  const HostInfo self_;

  std::atomic<bool> stop_;
  std::unique_ptr<Poller> poller_;

  Listeners listeners_;
  Connectors connectors_;
  Produced incoming_;  // created by listeners_
  Produced outgoing_;  // created by connectors_
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_H_
