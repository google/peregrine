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
#include "peregrine/src/internal/engine2/connection_planner.h"
#include "peregrine/src/internal/engine2/tcp_manager.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
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
  // Called with each ready channel between a local rail and a remote peer rail,
  // indicating whether this side initiated the connection.
  using OnChannelReadyFn = absl::AnyInvocable<void(
      const PeerId& peer, int32_t local_rail_id, int32_t remote_rail_id,
      bool is_outbound, std::unique_ptr<Channel> ch)>;

  // Called once per `Connect()` call that fails to open a channel to `peer`.
  using OnConnectFailedFn =
      absl::AnyInvocable<void(const PeerId& peer, int32_t local_rail_id,
                              int32_t remote_rail_id, absl::Status status)>;

  // Creates a channel manager for the local host, setting up data-plane
  // listeners and recording them on the host info. Both host info and metrics
  // must outlive the manager. Returns nullptr on failure.
  static std::unique_ptr<ChannelManager> Create(const Config& config,
                                                HostInfo& self,
                                                EngineMetrics& metrics);

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

  // Opens a channel described by `spec` without waiting. Reports the outcome
  // exactly once, to `on_channel_ready` or `on_connect_failed`, unless the
  // channel manager is destroyed first.
  void Connect(const ConnectionSpec& spec);

 private:
  explicit ChannelManager(std::unique_ptr<TcpManager> tcp_manager);

 private:
  std::unique_ptr<TcpManager> tcp_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
