#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_

#include <cstdint>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Creates data-plane channels.
//
// `ChannelManager` opens channels from a local rail to a remote peer's rail,
// and accepts channels opened by remote peers. It keeps no per-peer state:
// its caller decides which channels to open and owns the channels it gets.
//
// Thread-safety: This class is thread-safe.
class ChannelManager final {
 public:
  // Called with each accepted channel and the local rail it arrived on.
  using OnInboundReadyFn = absl::AnyInvocable<void(std::unique_ptr<Channel> ch,
                                                   uint32_t local_rail_id)>;

  // Called once per `Connect()` call with the channel or an error.
  using OnOutboundDoneFn = absl::AnyInvocable<void(
      const PeerId& peer, uint32_t local_rail_id, uint32_t remote_rail_id,
      absl::StatusOr<std::unique_ptr<Channel>> ch)>;

  // Creates a channel manager for the local host described by `self`, which
  // must outlive the channel manager. Returns nullptr on failure.
  //
  // Implementation details:
  // - Sets up the transports enabled by `config`: binds a listener per TCP
  //   NIC and opens RDMA devices.
  // - Records their endpoints in `self.data_plane_listeners`.
  static std::unique_ptr<ChannelManager> Create(const Config& config,
                                                HostInfo& self);

  DISALLOW_COPY(ChannelManager);
  DISALLOW_MOVE(ChannelManager);

  // Implementation details:
  // - Stops accepting channels and aborts pending connects; each pending
  //   connect reports an error to `on_outbound_done`.
  ~ChannelManager() = default;

  // Registers the completion callbacks. Must be called exactly once, before
  // `Connect()`. See assumptions.h for the callback contract.
  //
  // Implementation details:
  // - Starts accepting channels from remote peers, reporting each one to
  //   `on_inbound_ready`.
  void Start(OnInboundReadyFn on_inbound_ready,
             OnOutboundDoneFn on_outbound_done);

  // Opens a channel from local rail `local_rail_id` to rail `remote_rail_id`
  // of the peer described by `peer_info`. Returns without waiting, and reports
  // the outcome to `on_outbound_done` exactly once.
  //
  // Implementation details:
  // - Starts a non-blocking connect between the two rails' endpoints, bounded
  //   in time.
  // - Reports `UnimplementedError` for now.
  void Connect(const HostInfo& peer_info, uint32_t local_rail_id,
               uint32_t remote_rail_id);

 private:
  explicit ChannelManager(const HostInfo& self);

 private:
  const HostInfo& self_;

  OnInboundReadyFn on_inbound_ready_;
  OnOutboundDoneFn on_outbound_done_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_CHANNEL_MANAGER_H_
