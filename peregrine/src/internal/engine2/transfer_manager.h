#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_state.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/worker.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {

// Coordinates per-peer setup, work queuing, and workers.
//
// Thread-safety: This class is thread-safe.
class TransferManager final {
 public:
  // Creates a transfer manager for the local host (which must outlive it),
  // using the supplied trackers, metrics, and tracer to record transfer state.
  // Takes ownership of the channel manager and starts it. Returns nullptr on
  // failure.
  static std::unique_ptr<TransferManager> Create(
      const Config& config, const HostInfo& self, RequestTracker& outgoing,
      RequestTracker& incoming, EngineMetrics& metrics, TraceRecorder& tracer,
      GetPeerHostInfoFn get_peer_host_info_fn,
      std::unique_ptr<ChannelManager> channel_manager);

  DISALLOW_COPY(TransferManager);
  DISALLOW_MOVE(TransferManager);

  // Stops the setup thread, then the channel manager, then workers and peers.
  ~TransferManager();

  // Queues `post` (with pre-built `req_infos`) for transfer to `peer` without
  // blocking. Triggers peer setup if `peer` is idle, or wakes its workers if
  // ready.
  void Submit(const PeerId& peer, Post post,
              std::vector<RequestInfo> req_infos);

  // Cancels queued work for `handle` to `peer`.
  void Cancel(const PeerId& peer, Handle handle);

 private:
  // Queued cold-start setup request along with the triggering post's trace.
  struct SetupRequest {
    PeerId peer;
    Handle handle = kInvalidHandle;
    TraceContext trace_ctx;
  };

  TransferManager(const Config& config, const HostInfo& self,
                  RequestTracker& outgoing, RequestTracker& incoming,
                  EngineMetrics& metrics, TraceRecorder& tracer,
                  GetPeerHostInfoFn get_peer_host_info_fn,
                  std::unique_ptr<ChannelManager> channel_manager);

  // Returns the state of `peer`, creating it if needed.
  PeerState& getOrCreatePeer(const PeerId& peer) ABSL_LOCKS_EXCLUDED(peers_mu_);

  // Returns the state of `peer`, or nullptr if none exists.
  PeerState* findPeer(const PeerId& peer) ABSL_LOCKS_EXCLUDED(peers_mu_);

  // Background loop resolving host info and initiating async connects.
  void setupLoop();

  // Performs a blocking host info lookup and starts async connects for one
  // peer. Runs on `setup_thread_`.
  void setupPeer(const SetupRequest& req);

  // Marks the peer ready and hands the channel to a worker on the local rail.
  void onChannelReady(const PeerId& peer, int32_t local_rail_id,
                      bool is_outbound, std::unique_ptr<Channel> ch);

  // Records a closed channel on a worker and fails queued posts if no channels
  // or pending connects remain for the peer.
  void onChannelClosed(const PeerId& peer, Worker* worker);

  // Records a connect failure and fails queued posts if all connects failed.
  void onConnectFailed(const PeerId& peer, int32_t local_rail_id,
                       int32_t remote_rail_id, absl::Status status);

  // Marks every handle in `handles` failed in `outgoing_`.
  void failHandles(const std::vector<Handle>& handles);

  // Picks the next worker for `local_rail_id` (round-robin), falling back to
  // any worker if `local_rail_id` has none. Returns nullptr if there are no
  // workers.
  Worker* pickWorker(int32_t local_rail_id);

 private:
  // Workers assigned to a single local rail, with a round-robin cursor.
  struct RailWorkers {
    std::vector<Worker*> workers;
    std::atomic<size_t> next = 0;
  };

  const Config config_;
  const HostInfo& self_;
  RequestTracker& outgoing_;
  RequestTracker& incoming_;
  EngineMetrics& metrics_;
  TraceRecorder& tracer_;
  GetPeerHostInfoFn get_peer_host_info_fn_;

  // Per-peer state. Entries are never erased while alive, so pointers stay
  // valid without holding `peers_mu_`.
  absl::Mutex peers_mu_;
  absl::flat_hash_map<PeerId, std::unique_ptr<PeerState>> peers_
      ABSL_GUARDED_BY(peers_mu_);

  // Fixed after construction, so read without a lock.
  std::vector<std::unique_ptr<Worker>> workers_;
  absl::flat_hash_map<int32_t, std::unique_ptr<RailWorkers>> rail_workers_;

  std::unique_ptr<ChannelManager> channel_manager_;

  absl::Mutex setup_mu_;
  bool setup_stop_ ABSL_GUARDED_BY(setup_mu_) = false;
  std::deque<SetupRequest> setup_queue_ ABSL_GUARDED_BY(setup_mu_);
  util::Jthread setup_thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
