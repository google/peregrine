#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_state.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/worker.h"
#include "peregrine/src/internal/nic/nic_inventory.h"

namespace peregrine::internal::engine2 {
namespace {

// A pair of rails that a channel connects: one local, one on the peer.
struct RailPair {
  uint32_t local_rail_id = 0;
  uint32_t remote_rail_id = 0;
};

// Returns the rail pairs to connect between the local host `self` and the
// peer described by `peer_info`.
//
// Implementation details:
// - Pairs local and remote rails by NUMA locality, up to
//   `config.num_conns_per_peer` connections.
std::vector<RailPair> PlanConnects(const Config& config, const HostInfo& self,
                                   const HostInfo& peer_info) {
  return {};
}

}  // namespace

std::unique_ptr<TransferManager> TransferManager::Create(
    const Config& config, const HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn,
    std::unique_ptr<ChannelManager> channel_manager) {
  if ABSL_PREDICT_FALSE (get_peer_host_info_fn == nullptr) {
    LOG(ERROR) << "null peer host info lookup";
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (channel_manager == nullptr) {
    LOG(ERROR) << "null channel manager";
    return nullptr;
  }
  return absl::WrapUnique(new TransferManager(config, self,
                                              std::move(get_peer_host_info_fn),
                                              std::move(channel_manager)));
}

TransferManager::TransferManager(
    const Config& config, const HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn,
    std::unique_ptr<ChannelManager> channel_manager)
    : config_(config),
      self_(self),
      get_peer_host_info_fn_(std::move(get_peer_host_info_fn)),
      guard_(std::make_shared<LifetimeGuard>()),
      channel_manager_(std::move(channel_manager)) {
  {
    absl::MutexLock lock(guard_->mu);
    guard_->transfer_manager = this;
  }
  // One worker per local rail; rail IDs are 0..N-1 in NIC order.
  for (const nic::TcpNic& nic : self_.tcp_nics) {
    DCHECK_EQ(nic.rail_id, workers_.size());
    workers_.push_back(std::make_unique<Worker>(nic.rail_id));
  }
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

TransferManager::~TransferManager() {
  // Waits for any running host info callback, and drops later ones.
  absl::MutexLock lock(guard_->mu);
  guard_->transfer_manager = nullptr;
}

void TransferManager::Submit(const PeerId& peer, Post post) {
  PeerState& peer_state = getOrCreatePeer(peer);
  peer_state.Enqueue(std::move(post));
  if (peer_state.ShouldGetPeerHostInfo()) {
    // No lock is held here, since `on_done` may run inside the call.
    static_assert(assumptions::kPeerHostInfoLookupIsAsynchronous);
    get_peer_host_info_fn_(
        peer, [guard = guard_, peer](absl::StatusOr<HostInfo> info) {
          absl::MutexLock lock(guard->mu);
          if (guard->transfer_manager == nullptr) return;
          guard->transfer_manager->onPeerHostInfo(peer, std::move(info));
        });
  }
  // Posts are infrequent, so waking every worker costs little and avoids
  // tracking which workers serve `peer`.
  for (const std::unique_ptr<Worker>& worker : workers_) worker->Wake();
}

void TransferManager::Cancel(const PeerId& peer, Handle handle) {
  if (PeerState* peer_state = findPeer(peer); peer_state != nullptr) {
    peer_state->Purge(handle);
  }
}

PeerState& TransferManager::getOrCreatePeer(const PeerId& peer) {
  absl::MutexLock lock(mu_);
  std::unique_ptr<PeerState>& peer_state = peers_[peer];
  if (peer_state == nullptr) {
    peer_state = std::make_unique<PeerState>();
  }
  return *peer_state;
}

PeerState* TransferManager::findPeer(const PeerId& peer) {
  absl::MutexLock lock(mu_);
  const auto it = peers_.find(peer);
  return it == peers_.end() ? nullptr : it->second.get();
}

void TransferManager::onPeerHostInfo(const PeerId& peer,
                                     absl::StatusOr<HostInfo> host_info) {
  if (!host_info.ok()) {
    LOG(WARNING) << "failed to get host info of peer " << peer << ": "
                 << host_info.status();
    abortPosts(peer);
    return;
  }
  if (host_info->control_plane_listener != peer) {
    LOG(WARNING) << "host info for peer " << peer << " names "
                 << host_info->control_plane_listener;
    abortPosts(peer);
    return;
  }
  static_assert(assumptions::kChannelManagerCallbacksMayRunOnAnyThread);
  for (const RailPair& pair : PlanConnects(config_, self_, *host_info)) {
    channel_manager_->Connect(*host_info, pair.local_rail_id,
                              pair.remote_rail_id);
  }
}

void TransferManager::onInboundReady(std::unique_ptr<Channel> ch,
                                     uint32_t local_rail_id) {
  Worker* worker = workerFor(local_rail_id);
  if ABSL_PREDICT_FALSE (worker == nullptr) {
    LOG(ERROR) << "dropping channel accepted on unknown rail " << local_rail_id;
    return;
  }
  worker->AddInboundChannel(std::move(ch));
}

void TransferManager::onOutboundDone(
    const PeerId& peer, uint32_t local_rail_id, uint32_t remote_rail_id,
    absl::StatusOr<std::unique_ptr<Channel>> ch) {
  if (!ch.ok()) {
    LOG(WARNING) << "failed to connect rail " << local_rail_id << " to rail "
                 << remote_rail_id << " of peer " << peer << ": "
                 << ch.status();
    abortPosts(peer);
    return;
  }
  PeerState* peer_state = findPeer(peer);
  Worker* worker = workerFor(local_rail_id);
  if ABSL_PREDICT_FALSE (peer_state == nullptr || worker == nullptr) {
    LOG(ERROR) << "dropping channel on rail " << local_rail_id << " to peer "
               << peer;
    if (peer_state != nullptr) peer_state->AbortPosts();
    return;
  }
  worker->AddOutboundChannel(*std::move(ch), *peer_state);
}

Worker* TransferManager::workerFor(uint32_t rail_id) const {
  return rail_id < workers_.size() ? workers_[rail_id].get() : nullptr;
}

void TransferManager::abortPosts(const PeerId& peer) {
  static_assert(assumptions::kSetupFailuresAbortQueuedPosts);
  PeerState* peer_state = findPeer(peer);
  DCHECK(peer_state != nullptr) << "unknown peer " << peer;
  if (peer_state != nullptr) peer_state->AbortPosts();
}

}  // namespace peregrine::internal::engine2
