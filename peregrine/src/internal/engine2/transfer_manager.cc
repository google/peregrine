#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_state.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/worker.h"
#include "peregrine/src/internal/nic/nic_inventory.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {
namespace {

constexpr int kMaxWorkers = 16;

// Maps a listener interface to its rail ID (matching `TcpManager`).
std::optional<uint32_t> RailIdForListener(const HostInfo& host,
                                          std::string_view if_name,
                                          uint32_t ip_listener_idx) {
  if (host.tcp_nics.empty()) return ip_listener_idx;
  for (const nic::TcpNic& nic : host.tcp_nics) {
    if (nic.if_name == if_name) return nic.rail_id;
  }
  return std::nullopt;
}

// Returns the rail IDs with active IP listeners on `host` (or `{0}` if none).
std::vector<uint32_t> RailIds(const HostInfo& host) {
  std::vector<uint32_t> ids;
  uint32_t idx = 0;
  for (const NicInfo& listener : host.data_plane_listeners) {
    if (listener.type != util::NicType::kIP || listener.endpoints.empty()) {
      continue;
    }
    if (const std::optional<uint32_t> rail =
            RailIdForListener(host, listener.name, idx);
        rail.has_value()) {
      ids.push_back(*rail);
    }
    ++idx;
  }
  if (ids.empty()) return {0};
  return ids;
}

}  // namespace

std::unique_ptr<TransferManager> TransferManager::Create(
    const Config& config, const HostInfo& self, RequestTracker& outgoing,
    RequestTracker& incoming, GetPeerHostInfoFn get_peer_host_info_fn,
    std::unique_ptr<ChannelManager> channel_manager) {
  if (ABSL_PREDICT_FALSE(!config.IsValid())) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(get_peer_host_info_fn == nullptr)) {
    LOG(ERROR) << "null peer host info lookup";
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(channel_manager == nullptr)) {
    LOG(ERROR) << "null channel manager";
    return nullptr;
  }
  return absl::WrapUnique(new TransferManager(config, self, outgoing, incoming,
                                              std::move(get_peer_host_info_fn),
                                              std::move(channel_manager)));
}

TransferManager::TransferManager(
    const Config& config, const HostInfo& self, RequestTracker& outgoing,
    RequestTracker& incoming, GetPeerHostInfoFn get_peer_host_info_fn,
    std::unique_ptr<ChannelManager> channel_manager)
    : config_(config),
      self_(self),
      outgoing_(outgoing),
      incoming_(incoming),
      get_peer_host_info_fn_(std::move(get_peer_host_info_fn)),
      channel_manager_(std::move(channel_manager)) {
  const std::vector<uint32_t> rails = RailIds(self_);
  const size_t total_workers = std::max<size_t>(
      rails.size(), std::clamp(config_.num_conns_per_peer, 1, kMaxWorkers));
  workers_.reserve(total_workers);
  for (size_t i = 0; i < total_workers; ++i) {
    const uint32_t rail_id = rails[i % rails.size()];
    workers_.push_back(std::make_unique<Worker>(static_cast<int>(i), rail_id,
                                                self_, outgoing_, incoming_));
    std::unique_ptr<RailWorkers>& pool = rail_workers_[rail_id];
    if (pool == nullptr) pool = std::make_unique<RailWorkers>();
    pool->workers.push_back(workers_.back().get());
  }

  channel_manager_->Start(
      [this](const PeerId& peer, uint32_t local_rail_id,
             uint32_t /*remote_rail_id*/, std::unique_ptr<Channel> ch) {
        onChannelReady(peer, local_rail_id, std::move(ch));
      },
      [this](const PeerId& peer, uint32_t local_rail_id,
             uint32_t remote_rail_id, absl::Status status) {
        onConnectFailed(peer, local_rail_id, remote_rail_id, std::move(status));
      });

  setup_thread_ = util::Jthread([this] { setupLoop(); });
}

TransferManager::~TransferManager() {
  // 1. Stop setup thread first while `channel_manager_` is still alive.
  {
    absl::MutexLock lock(setup_mu_);
    setup_stop_ = true;
    setup_queue_.clear();
  }
  if (setup_thread_.joinable()) setup_thread_.join();

  // 2. Stop channel manager so no more callbacks fire into workers or peers.
  channel_manager_.reset();

  // 3. Destroy workers before peers.
  workers_.clear();
}

void TransferManager::Submit(const PeerId& peer, Post post,
                             std::vector<RequestInfo> req_infos) {
  auto queued = std::make_shared<QueuedPost>(
      QueuedPost{.post = std::move(post), .req_infos = std::move(req_infos)});
  PeerState& peer_state = getOrCreatePeer(peer);

  PeerState::WorkerList to_wake;
  if (peer_state.Enqueue(std::move(queued), to_wake)) {
    absl::MutexLock lock(setup_mu_);
    if (!setup_stop_) setup_queue_.push_back(peer);
  }
  for (Worker* worker : to_wake) worker->Wake();
}

void TransferManager::Cancel(const PeerId& peer, const Handle handle) {
  if (PeerState* peer_state = findPeer(peer); peer_state != nullptr) {
    peer_state->Purge(handle);
  }
}

PeerState& TransferManager::getOrCreatePeer(const PeerId& peer) {
  absl::MutexLock lock(peers_mu_);
  std::unique_ptr<PeerState>& peer_state = peers_[peer];
  if (peer_state == nullptr) peer_state = std::make_unique<PeerState>();
  return *peer_state;
}

PeerState* TransferManager::findPeer(const PeerId& peer) {
  absl::MutexLock lock(peers_mu_);
  const auto it = peers_.find(peer);
  return it == peers_.end() ? nullptr : it->second.get();
}

void TransferManager::setupLoop() {
  static_assert(assumptions::kPeerHostInfoLookupRunsOnSetupThread);
  while (true) {
    PeerId peer;
    {
      absl::MutexLock lock(setup_mu_);
      const auto ready = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(setup_mu_) {
        return setup_stop_ || !setup_queue_.empty();
      };
      setup_mu_.Await(absl::Condition(&ready));
      if (setup_stop_) return;
      peer = setup_queue_.front();
      setup_queue_.pop_front();
    }
    setupPeer(peer);
  }
}

void TransferManager::setupPeer(const PeerId& peer) {
  PeerState* peer_state = findPeer(peer);
  if (ABSL_PREDICT_FALSE(peer_state == nullptr)) return;
  if (peer_state->IsReady()) return;  // an inbound channel arrived first

  absl::StatusOr<HostInfo> host_info = get_peer_host_info_fn_(peer);
  if (!host_info.ok()) {
    LOG(WARNING) << "failed to get host info of peer " << peer << ": "
                 << host_info.status();
    failHandles(peer_state->AbortSetup());
    return;
  }
  if (host_info->control_plane_listener != peer) {
    LOG(WARNING) << "host info for peer " << peer << " names "
                 << host_info->control_plane_listener;
    failHandles(peer_state->AbortSetup());
    return;
  }
  if (peer_state->IsReady())
    return;  // an inbound channel arrived during lookup

  const std::vector<uint32_t> local_rails = RailIds(self_);
  const std::vector<uint32_t> remote_rails = RailIds(*host_info);
  const int n = config_.num_conns_per_peer;
  peer_state->OnConnectsStarted(n);
  static_assert(assumptions::kChannelManagerCallbacksMayRunOnAnyThread);
  for (int i = 0; i < n; ++i) {
    channel_manager_->Connect(*host_info, local_rails[i % local_rails.size()],
                              remote_rails[i % remote_rails.size()]);
  }
}

void TransferManager::onChannelReady(const PeerId& peer,
                                     const uint32_t local_rail_id,
                                     std::unique_ptr<Channel> ch) {
  static_assert(assumptions::kDuplicateChannelsAllowedOnSimultaneousConnect);
  Worker* worker = pickWorker(local_rail_id);
  if (ABSL_PREDICT_FALSE(worker == nullptr)) {
    LOG(ERROR) << "dropping channel to peer " << peer << ": no workers";
    return;
  }
  PeerState& peer_state = getOrCreatePeer(peer);
  peer_state.OnChannelReady(worker);
  worker->AddChannel(peer, std::move(ch), peer_state.work_queue());
}

void TransferManager::onConnectFailed(const PeerId& peer,
                                      const uint32_t local_rail_id,
                                      const uint32_t remote_rail_id,
                                      absl::Status status) {
  LOG(WARNING) << "failed to connect rail " << local_rail_id << " to rail "
               << remote_rail_id << " of peer " << peer << ": " << status;
  if (PeerState* peer_state = findPeer(peer); peer_state != nullptr) {
    failHandles(peer_state->OnConnectFailed());
  }
}

void TransferManager::failHandles(const std::vector<Handle>& handles) {
  for (const Handle handle : handles) {
    outgoing_.Fail(handle, Status::kFailure);
  }
}

Worker* TransferManager::pickWorker(const uint32_t local_rail_id) {
  RailWorkers* pool = nullptr;
  if (const auto it = rail_workers_.find(local_rail_id);
      it != rail_workers_.end() && !it->second->workers.empty()) {
    pool = it->second.get();
  } else if (!rail_workers_.empty() &&
             !rail_workers_.begin()->second->workers.empty()) {
    pool = rail_workers_.begin()->second.get();
  }
  if (pool == nullptr) return nullptr;
  const size_t idx = pool->next.fetch_add(1, std::memory_order_relaxed);
  return pool->workers[idx % pool->workers.size()];
}

}  // namespace peregrine::internal::engine2
