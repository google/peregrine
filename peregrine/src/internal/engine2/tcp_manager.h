#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/nic/nic_inventory.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {

// Non-blocking TCP listener, connection, and peer-identity handshake manager.
//
// Overview:
// 1. `Create(tcp_nics, self)`: Binds non-blocking listening sockets across
//    `tcp_nics` on ephemeral ports (`port = 0`; falls back to
//    `self.control_plane_listener`'s IP on `"lo"` when `tcp_nics` is empty) and
//    populates `self.data_plane_listeners` + `self.tcp_nics`.
// 2. `Start(self_peer, on_inbound_ready)`: Pre-serializes `self_peer` (20-byte
//    `flatbuf::Endpoint` in `src/internal/chunk/header.fbs`) and starts a
//    background epoll loop accepting incoming sockets, reading each peer's 20B
//    control-plane `Endpoint`, setting `ch->SetPeer(peer)`, unregistering the
//    FD from `TcpManager`'s poller, and invoking
//    `on_inbound_ready(std::move(ch), local_rail_id)`.
// 3. `Connect(local_ep, remote_ep, timeout, on_done)`: Initiates a non-blocking
//    connect from `local_ep` to `remote_ep`, queues it to the epoll loop, sends
//    the pre-serialized 20B `self_peer`, and invokes `on_done` asynchronously
//    once ready (or with an error on socket failure, `timeout` expiry, or
//    `Stop()`).
//
// Thread-safety: This class is thread-safe.
class TcpManager final {
 public:
  // Wire size of the serialized control-plane Endpoint (`flatbuf::Endpoint`).
  static constexpr size_t kPeerHandshakeSize = 20;

  // Invoked when an accepted inbound connection finishes its 20B peer
  // handshake.
  using OnInboundReadyFn = absl::AnyInvocable<void(std::unique_ptr<Channel> ch,
                                                   uint32_t local_rail_id)>;
  // Invoked when a non-blocking outbound `Connect()` finishes or fails.
  using OnConnectDoneFn =
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Channel>> ch)>;

  // Binds per-rail non-blocking TCP listeners and populates `self`.
  // Call `Start(...)` once `ChannelManager` is ready to receive channels.
  static std::unique_ptr<TcpManager> Create(
      absl::Span<const nic::TcpNic> tcp_nics, HostInfo& self);

  DISALLOW_COPY(TcpManager);
  DISALLOW_MOVE(TcpManager);

  ~TcpManager();

  // Registers our control-plane `self_peer` identity and `on_inbound_ready`,
  // and starts the background epoll loop.
  void Start(const Endpoint& self_peer, OnInboundReadyFn on_inbound_ready);

  // Stops the epoll loop, fails any pending outbound connects via `on_done`,
  // and closes all owned listeners and in-progress handshake sockets.
  // Idempotent; also invoked by `~TcpManager()`.
  void Stop() ABSL_LOCKS_EXCLUDED(mu_);

  // Initiates a non-blocking TCP connect + 20B `self_peer` handshake from
  // `local_ep` to `remote_ep`, invoking `on_done` asynchronously on completion,
  // error, or `timeout`.
  void Connect(const Endpoint& local_ep, const Endpoint& remote_ep,
               absl::Duration timeout, OnConnectDoneFn on_done)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Per-rail non-blocking listening socket bound during `Create()`.
  struct Listener {
    std::unique_ptr<TcpSocket> socket;
    uint32_t local_rail_id = 0;
  };

  // Accepted inbound socket reading the 20B peer-identity handshake.
  struct InboundConn {
    std::unique_ptr<TcpSocket> socket;
    uint32_t local_rail_id = 0;
    std::array<uint8_t, kPeerHandshakeSize> buf{};
    size_t bytes_done = 0;
    absl::Time deadline;
  };

  // Non-blocking outbound socket completing TCP connect + 20B handshake.
  struct OutboundConn {
    std::unique_ptr<TcpSocket> socket;
    size_t bytes_sent = 0;  // Offset into `self_peer_buf_`
    absl::Time deadline;
    OnConnectDoneFn on_done;
  };

  TcpManager() = default;

  // Background epoll loop driving accepts, non-blocking connects, and 20B peer
  // handshakes.
  void runLoop();

  // Moves queued `OutboundConn`s from `pending_outbound_` onto `poller_`.
  void drainPendingOutbound() ABSL_LOCKS_EXCLUDED(mu_);

  // Accepts incoming socket(s) on `listener_fd` and registers `InboundConn`s.
  void handleAccept(fd_t listener_fd);

  // Advances 20B handshake read and invokes `on_inbound_ready_fn_` when done.
  void handleInbound(fd_t fd, uint32_t events);

  // Checks `SO_ERROR`, advances 20B handshake write, and invokes `on_done`.
  void handleOutbound(fd_t fd, uint32_t events);

  // Fails timed-out outbound connects and closes stalled inbound handshakes.
  void expireDeadlines(absl::Time now);

  std::unique_ptr<Poller> poller_;
  // Wakes `runLoop()` from `epoll_wait` on new `Connect()` calls or `Stop()`.
  fd_t event_fd_{-1};
  std::atomic<bool> stop_{false};
  // Pre-serialized 20B `self_peer` sent on every outbound handshake.
  std::array<uint8_t, kPeerHandshakeSize> self_peer_buf_{};
  OnInboundReadyFn on_inbound_ready_fn_;

  // Owned exclusively by `runLoop()` once `Start()` is called.
  absl::flat_hash_map<fd_t, Listener> listeners_;
  absl::flat_hash_map<fd_t, InboundConn> inbound_conns_;
  absl::flat_hash_map<fd_t, OutboundConn> outbound_conns_;

  absl::Mutex mu_;
  // Rejects new `Connect()` calls once `Stop()` begins.
  bool sealed_ ABSL_GUARDED_BY(mu_) = false;
  // Outbound connects queued by `Connect()` for `runLoop()` registration.
  std::vector<OutboundConn> pending_outbound_ ABSL_GUARDED_BY(mu_);

  util::Jthread thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_
