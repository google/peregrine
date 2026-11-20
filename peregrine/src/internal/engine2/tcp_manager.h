#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {

// Manages asynchronous TCP listeners and channel establishment.
//
// Thread-safety: This class is thread-safe.
class TcpManager final {
 public:
  // Called with each ready channel between `local_rail_id` and `peer`'s
  // `remote_rail_id`.
  using OnChannelReadyFn = absl::AnyInvocable<void(
      const PeerId& peer, uint32_t local_rail_id, uint32_t remote_rail_id,
      std::unique_ptr<Channel> ch)>;

  // Called once per `Connect()` call that fails to open a channel to `peer`.
  using OnConnectFailedFn =
      absl::AnyInvocable<void(const PeerId& peer, uint32_t local_rail_id,
                              uint32_t remote_rail_id, absl::Status status)>;

  // Creates a tcp manager with per-NIC non-blocking listening sockets, and
  // fills in `self.data_plane_listeners`. `self` must outlive the manager.
  // Returns nullptr on failure.
  static std::unique_ptr<TcpManager> Create(const Config& config,
                                            HostInfo& self);

  DISALLOW_COPY(TcpManager);
  DISALLOW_MOVE(TcpManager);

  // Stops the manager and closes all listeners and in-flight channel setups
  // without invoking callbacks.
  ~TcpManager();

  // Registers the callbacks and starts the event loop. Must be called at most
  // once, before `Connect()`.
  void Start(OnChannelReadyFn on_channel_ready,
             OnConnectFailedFn on_connect_failed);

  // Stops the manager. Idempotent; also called by `~TcpManager()`.
  void Stop() ABSL_LOCKS_EXCLUDED(mu_);

  // Connects `local_rail_id` to `remote_rail_id` of `peer_info` without
  // blocking. Reports the outcome on the internal poll thread, to
  // `on_channel_ready` or `on_connect_failed`, unless stopped first.
  void Connect(const HostInfo& peer_info, uint32_t local_rail_id,
               uint32_t remote_rail_id) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  static constexpr size_t kHelloSize = 24;

  // Non-blocking listening socket and its bound local endpoint.
  struct Listener {
    std::unique_ptr<TcpSocket> socket;
    Endpoint endpoint;
  };

  // Outbound connect request queued by `Connect()` for the poll thread.
  struct ConnectRequest {
    HostInfo peer_info;
    uint32_t local_rail_id = 0;
    uint32_t remote_rail_id = 0;
  };

  // Identifies a remote endpoint's peer and rail in the handshake.
  struct PeerRail {
    PeerId peer;
    uint32_t rail_id = 0;
    bool operator==(const PeerRail&) const = default;
  };

  // One in-flight channel establishment: TCP connect (outbound) + peer hello.
  struct ChannelInit {
    enum class State { kConnecting, kHandshaking };

    State state = State::kHandshaking;
    std::unique_ptr<TcpSocket> socket;
    uint32_t local_rail_id = 0;
    std::optional<PeerRail> expected;  // set iff outbound
    absl::Duration expiration;
    std::array<char, kHelloSize> hello = {};
    size_t received = 0;
  };

  TcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
             fd_t event_fd, absl::flat_hash_map<fd_t, Listener> listeners);

  // Returns true iff `Stop()` has been called.
  bool isStopped() const { return stop_.load(std::memory_order_acquire); }

  // Signals `event_fd_` to wake `runLoop()`.
  void wake();

  // Single epoll loop driving accepts, outbound connects, handshakes, and
  // timeouts until `Stop()`.
  void runLoop();

  // Drains `pending_` and starts each queued outbound connect.
  void drainPending() ABSL_LOCKS_EXCLUDED(mu_);

  // Resolves rail endpoints, creates a non-blocking socket, and initiates TCP
  // connect for `req`.
  void startConnect(const ConnectRequest& req);

  // Sends our hello on a newly connected/accepted socket and registers it on
  // `poller_` to read the peer's hello.
  void beginHandshake(ChannelInit init);

  // Accepts all pending incoming connections on listening socket `fd`.
  bool handleAccept(fd_t fd, uint32_t flag);

  // Advances an in-flight `ChannelInit` (connect completion or hello read).
  bool handleInit(fd_t fd, uint32_t flag);

  // Validates the peer's hello (on OK) and invokes `on_channel_ready_` or
  // `on_connect_failed_`.
  void finishInit(ChannelInit init, absl::Status status);

  // Fails any in-flight `ChannelInit` past its deadline.
  void expireInits();

 private:
  const HostInfo& self_;
  const std::unique_ptr<Poller> poller_;
  const fd_t event_fd_;

  OnChannelReadyFn on_channel_ready_;
  OnConnectFailedFn on_connect_failed_;

  std::atomic<bool> stop_ = false;
  absl::Mutex mu_;
  bool sealed_ ABSL_GUARDED_BY(mu_) = false;
  std::vector<ConnectRequest> pending_ ABSL_GUARDED_BY(mu_);

  // Owned exclusively by `runLoop()` once `Start()` is called.
  absl::flat_hash_map<fd_t, Listener> listeners_;
  absl::flat_hash_map<fd_t, ChannelInit> inits_;

  // Declared last so it is joined before members are destroyed.
  util::Jthread poll_thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TCP_MANAGER_H_
