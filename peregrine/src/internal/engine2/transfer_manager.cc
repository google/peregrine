#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/channel_manager.h"

namespace peregrine::internal::engine2 {

std::unique_ptr<TransferManager> TransferManager::Create(
    const Config& config, const HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn,
    std::unique_ptr<ChannelManager> channel_manager) {
  if ABSL_PREDICT_FALSE (channel_manager == nullptr) {
    LOG(ERROR) << "null channel manager";
    return nullptr;
  }
  return absl::WrapUnique(new TransferManager(std::move(channel_manager)));
}

TransferManager::TransferManager(
    std::unique_ptr<ChannelManager> channel_manager)
    : channel_manager_(std::move(channel_manager)) {
  channel_manager_->Start(
      [this](std::unique_ptr<Channel> ch, uint32_t local_rail_id) {
        onInboundReady(std::move(ch), local_rail_id);
      },
      [this](const PeerId& peer, uint32_t local_rail_id,
             uint32_t remote_rail_id,
             absl::StatusOr<std::unique_ptr<Channel>> ch) {
        onOutboundDone(peer, local_rail_id, remote_rail_id, std::move(ch));
      });
}

void TransferManager::Submit(const PeerId& peer, Post post) {}

void TransferManager::Cancel(const PeerId& peer, Handle handle) {}

void TransferManager::onInboundReady(std::unique_ptr<Channel> ch,
                                     uint32_t local_rail_id) {}

void TransferManager::onOutboundDone(
    const PeerId& peer, uint32_t local_rail_id, uint32_t remote_rail_id,
    absl::StatusOr<std::unique_ptr<Channel>> ch) {}

}  // namespace peregrine::internal::engine2
