#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/tcp_manager.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Policy for striping `num_conns` across local and remote rails in `Connect()`.
enum class ConnPolicy : uint8_t {
  // Pairs each enabled local rail (`TcpNic.rail_id`) with the matching remote
  // `rail_id` (falling back to modulo striping when rail sets differ).
  kRailAligned = 0,

  // Stripes round-robin across compatible `(local_rail_id, remote_rail_id)`
  // pairs.
  kRoundRobin = 1,
};

// Asynchronous peer topology resolution and multi-rail channel setup manager.
//
// Expected `HostInfo` shape (see `src/internal/base/hostinfo.h`,
// `src/internal/control/message_internal.proto`, and
// `src/internal/nic/nic_inventory.h`):
//
//   struct HostInfo {
//     Endpoint control_plane_listener;
//     std::vector<NicInfo> data_plane_listeners;  // Bound (ip, port) per NIC
//     std::vector<nic::TcpNic> tcp_nics;          // rail_id, PciLocation, IPs
//     std::vector<nic::RdmaNic> rdma_nics;        // Future RDMA support
//   };
//
// Startup sequence (`self` is already finalized and `const` when passed in):
// 1. `auto tcp_mgr = TcpManager::Create(tcp_nics, self);` populates
//    `self.tcp_nics` + `self.data_plane_listeners`.
// 2. `control.Start();` populates `self.control_plane_listener`'s port.
// 3. `ChannelManager::Create(config, self, std::move(tcp_mgr), ...)` stores
//    `const HostInfo self_` and calls
//    `tcp_mgr_->Start(self.control_plane_listener, ...)` to receive inbound
//    channels.
//
// Connection flow:
// - On `Connect(peer, ...)`:
//   - Warm (`peer` cached in `peer_host_info_`): `Connect(peer, num_conns,
//     policy)` calls `spread(...)`, which dispatches `Connect(peer,
//     local_rail_id, remote_rail_id)` -> non-blocking
//     `tcp_mgr_->Connect(..., callback)`.
//   - Cold (`peer` not cached): queues the request under `mu_` and issues a
//     single async `get_peer_host_info_fn_(peer, ...)` call (deduplicated via
//     `pending_connects_`). Once `HostInfo` arrives in `onPeerHostInfoFetched`,
//     caches it and calls `spread(...)`.
// - Completion callback from `tcp_mgr_->Connect(...)`:
//   - Success: sets `ch->SetPeer(peer)` and invokes
//     `on_channel_ready_fn_(std::move(ch), local_rail_id)` immediately (pull
//     model does not wait for sibling connections in a batch) before
//     decrementing `connecting_count_[peer]`.
//   - Failure: increments `metrics_.tcp_connect_failures`, decrements
//     `connecting_count_[peer]`, and invokes `on_connect_failed_fn_` only when
//     `connecting_count_[peer]` reaches `0`.
// - Shutdown: `~ChannelManager()` sets `stopping_ = true`, awaits
//   `inflight_rpcs_ == 0`, and calls `tcp_mgr_->Stop()` before member teardown.
//   Never invoke external callbacks while holding `mu_`.
//
// Thread-safety: This class is thread-safe.
class ChannelManager final {
 public:
  // A peer is uniquely identified by its control-plane listener Endpoint.
  using PeerId = Endpoint;

  // Async hook to fetch a remote peer's `HostInfo` over the control plane.
  using GetPeerHostInfoFn = absl::AnyInvocable<void(
      const PeerId& peer,
      absl::AnyInvocable<void(absl::StatusOr<HostInfo>)> on_fetched)>;

  // Invoked immediately as each inbound or outbound channel becomes ready.
  using OnChannelReadyFn = absl::AnyInvocable<void(std::unique_ptr<Channel> ch,
                                                   uint32_t local_rail_id)>;

  // Invoked only when an outbound attempt (or `HostInfo` lookup) fails AND
  // `ConnectingCount(peer)` reaches `0`.
  using OnConnectFailedFn =
      absl::AnyInvocable<void(const PeerId& peer, Status status)>;

  // Creates a `ChannelManager` and starts `tcp_mgr` to accept inbound channels.
  static std::unique_ptr<ChannelManager> Create(
      const Config& config, const HostInfo& self,
      std::unique_ptr<TcpManager> tcp_mgr, EngineMetrics& metrics,
      GetPeerHostInfoFn get_peer_host_info_fn,
      OnChannelReadyFn on_channel_ready_fn,
      OnConnectFailedFn on_connect_failed_fn);

  DISALLOW_COPY(ChannelManager);
  DISALLOW_MOVE(ChannelManager);

  // Drains in-flight `get_peer_host_info_fn_` RPCs and shuts down `tcp_mgr_`.
  ~ChannelManager();

  // Dispatches `num_conns` non-blocking outbound connects to `peer` striped per
  // `policy` (fetching `peer`'s `HostInfo` asynchronously first if cold).
  void Connect(const PeerId& peer, int num_conns,
               ConnPolicy policy = ConnPolicy::kRailAligned)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Dispatches 1 non-blocking outbound connect from `local_rail_id` to `peer`'s
  // `remote_rail_id` (requires `peer`'s `HostInfo` to be cached).
  void Connect(const PeerId& peer, uint32_t local_rail_id,
               uint32_t remote_rail_id) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns the number of currently in-flight outbound connection attempts to
  // `peer` (both awaiting `GetPeerHostInfoFn` and in-progress in `tcp_mgr_`).
  int ConnectingCount(const PeerId& peer) const ABSL_LOCKS_EXCLUDED(mu_);

  // Evicts cached `HostInfo` for `peer` (e.g. after peer restart or failure).
  void InvalidatePeer(const PeerId& peer) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Cold-path connect request queued while `GetPeerHostInfoFn` is in flight.
  struct PendingConnect {
    int num_conns = 1;
    ConnPolicy policy = ConnPolicy::kRailAligned;
  };

  ChannelManager(const Config& config, const HostInfo& self,
                 std::unique_ptr<TcpManager> tcp_mgr, EngineMetrics& metrics,
                 GetPeerHostInfoFn get_peer_host_info_fn,
                 OnChannelReadyFn on_channel_ready_fn,
                 OnConnectFailedFn on_connect_failed_fn);

  // Stripes `num_conns` across rails per `policy` and dispatches
  // `Connect(peer, local_rail_id, remote_rail_id)` for each pair.
  void spread(const HostInfo& peer_info, int num_conns, ConnPolicy policy)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Caches resolved `HostInfo` and dispatches queued `PendingConnect`s (or
  // fails them on RPC error).
  void onPeerHostInfoFetched(const PeerId& peer,
                             absl::StatusOr<HostInfo> peer_host_info)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Handles a ready inbound channel from `tcp_mgr_` and delivers it via
  // `on_channel_ready_fn_` so `Scheduler` registers it on a `Worker`.
  void onInboundConnectDone(std::unique_ptr<Channel> ch,
                            uint32_t local_rail_id);

  // Handles completion of a single outbound `tcp_mgr_->Connect` attempt,
  // delivering the ready channel or updating failure counters.
  void onOutboundConnectDone(const PeerId& peer, uint32_t local_rail_id,
                             absl::StatusOr<std::unique_ptr<Channel>> ch)
      ABSL_LOCKS_EXCLUDED(mu_);

  const Config config_;
  // Local node topology and bound listener endpoints.
  const HostInfo self_;
  EngineMetrics& metrics_;
  std::unique_ptr<TcpManager> tcp_mgr_;

  GetPeerHostInfoFn get_peer_host_info_fn_;
  OnChannelReadyFn on_channel_ready_fn_;
  OnConnectFailedFn on_connect_failed_fn_;

  mutable absl::Mutex mu_;
  // Rejects new connects and signals teardown in `~ChannelManager()`.
  bool stopping_ ABSL_GUARDED_BY(mu_) = false;
  // Active `get_peer_host_info_fn_` RPC count; `~ChannelManager()` awaits 0.
  size_t inflight_rpcs_ ABSL_GUARDED_BY(mu_) = 0;
  // Cached remote `HostInfo` per peer.
  absl::flat_hash_map<PeerId, HostInfo> peer_host_info_ ABSL_GUARDED_BY(mu_);
  // Connect requests queued while a peer's `HostInfo` RPC is in flight (keys
  // also deduplicate concurrent RPCs).
  absl::flat_hash_map<PeerId, std::vector<PendingConnect>> pending_connects_
      ABSL_GUARDED_BY(mu_);
  // In-flight outbound connection attempts per peer (RPC wait + TCP handshake).
  absl::flat_hash_map<PeerId, int> connecting_count_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
