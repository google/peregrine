#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_

#include <cstdint>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/tcp_manager.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Creates data-plane channels.
//
// Opens channels to remote peers and accepts channels opened by them without
// blocking. Keeps no per-peer state: its caller decides which channels to open
// and owns the channels it gets.
//
// Thread-safety: This class is thread-safe.
class ChannelManager final {
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

  // Creates a channel manager for the local host described by `self`, which
  // must outlive it. Sets up the data-plane listeners and records them in
  // `self.data_plane_listeners`. Returns nullptr on failure.
  static std::unique_ptr<ChannelManager> Create(const Config& config,
                                                HostInfo& self);

  DISALLOW_COPY(ChannelManager);
  DISALLOW_MOVE(ChannelManager);

  // Stops accepting channels and drops pending connects and handshakes without
  // reporting them, so shutdown does not look like a connect failure.
  ~ChannelManager() = default;

  // Registers the callbacks and starts accepting channels. Must be called
  // exactly once, before `Connect()`. See assumptions.h for the callback
  // contract.
  void Start(OnChannelReadyFn on_channel_ready,
             OnConnectFailedFn on_connect_failed);

  // Opens a channel from local rail `local_rail_id` to rail `remote_rail_id`
  // of the peer described by `peer_info`. Returns without waiting. Reports the
  // outcome exactly once, to `on_channel_ready` or `on_connect_failed`, unless
  // the channel manager is destroyed first.
  void Connect(const HostInfo& peer_info, uint32_t local_rail_id,
               uint32_t remote_rail_id);

 private:
  explicit ChannelManager(std::unique_ptr<TcpManager> tcp_manager);

 private:
  std::unique_ptr<TcpManager> tcp_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
