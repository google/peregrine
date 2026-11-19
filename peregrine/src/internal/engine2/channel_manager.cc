#include "peregrine/src/internal/engine2/channel_manager.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/assumptions.h"

namespace peregrine::internal::engine2 {

std::unique_ptr<ChannelManager> ChannelManager::Create(const Config& config,
                                                       HostInfo& self) {
  if ABSL_PREDICT_FALSE (!config.IsValid()) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  return absl::WrapUnique(new ChannelManager(self));
}

ChannelManager::ChannelManager(const HostInfo& self) : self_(self) {}

void ChannelManager::Start(OnInboundReadyFn on_inbound_ready,
                           OnOutboundDoneFn on_outbound_done) {
  static_assert(assumptions::kChannelManagerCallbacksMayRunOnAnyThread);
  DCHECK(on_inbound_ready != nullptr);
  DCHECK(on_outbound_done != nullptr);
  DCHECK(on_outbound_done_ == nullptr) << "Start() called twice";
  on_inbound_ready_ = std::move(on_inbound_ready);
  on_outbound_done_ = std::move(on_outbound_done);
}

void ChannelManager::Connect(const HostInfo& peer_info,
                             const uint32_t local_rail_id,
                             const uint32_t remote_rail_id) {
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);
  DCHECK(self_.control_plane_listener.HasNonzeroIpPort());
  DCHECK(peer_info.control_plane_listener.HasNonzeroIpPort());
  DCHECK(on_outbound_done_ != nullptr) << "Connect() called before Start()";
  on_outbound_done_(peer_info.control_plane_listener, local_rail_id,
                    remote_rail_id,
                    absl::UnimplementedError("ChannelManager::Connect"));
}

}  // namespace peregrine::internal::engine2
