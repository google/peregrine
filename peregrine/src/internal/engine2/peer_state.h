#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_

#include <memory>
#include <optional>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/worker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Per-peer connection setup state, assigned workers, and work queue.
//
// Thread-safety: This class is thread-safe.
class PeerState final {
 public:
  using WorkerList = absl::InlinedVector<Worker*, 16>;

  // Trace metadata of the post that triggered cold-start peer setup.
  struct SetupTrace {
    Handle handle = kInvalidHandle;
    TraceContext trace_ctx;
  };

  PeerState() = default;

  DISALLOW_COPY(PeerState);
  DISALLOW_MOVE(PeerState);

  ~PeerState() = default;

  // Returns the peer's outgoing work queue. Safe to pass to workers as long as
  // this PeerState outlives them.
  PeerWorkQueue& work_queue() { return work_queue_; }

  // Queues `post` for transfer. Returns true iff the peer transitioned from
  // idle to setting up (caller should start peer setup). If the peer is
  // already ready, appends its assigned workers to `to_wake`.
  bool Enqueue(std::shared_ptr<QueuedPost> post, WorkerList& to_wake)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Begins cold-start setup if the peer is still setting up (not yet ready),
  // saving `trace` (when traced) so the first `OnChannelReady()` can finish it.
  // Returns false if a channel is already ready or setup is not active.
  bool TryBeginSetup(const SetupTrace& trace) ABSL_LOCKS_EXCLUDED(mu_);

  // Records outbound connect attempts started for this peer while setup is in
  // progress. Returns false if setup is no longer active.
  bool OnConnectsStarted(int count) ABSL_LOCKS_EXCLUDED(mu_);

  // Marks the peer ready (covers both outbound and inbound channels), updates
  // pending outbound connect accounting, and records the worker as serving a
  // channel to this peer. Returns the active setup trace once if a traced
  // cold-start setup was in progress.
  std::optional<SetupTrace> OnChannelReady(Worker* worker,
                                           bool is_outbound = false)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Records a closed channel on the given worker. Removes the worker once it no
  // longer serves any channel to this peer. When the last active channel closes
  // and no outbound connects remain in flight, resets to idle, drops queued
  // posts, and returns their handles to fail.
  std::vector<Handle> OnChannelClosed(Worker* worker) ABSL_LOCKS_EXCLUDED(mu_);

  // Records one failed outbound connect. If no connects remain in flight and
  // the peer has no ready channels, resets to idle, drops queued posts, and
  // returns their handles to fail.
  std::vector<Handle> OnConnectFailed() ABSL_LOCKS_EXCLUDED(mu_);

  // Aborts an in-flight setup (e.g., host info lookup failure). No-op if the
  // peer is not actively setting up without pending connects; otherwise resets
  // to idle, drops queued posts, and returns their handles to fail.
  std::vector<Handle> AbortSetup() ABSL_LOCKS_EXCLUDED(mu_);

  // Drops all queued posts matching `handle`. Already-claimed chunks are not
  // affected.
  void Purge(Handle handle);

  // Returns true iff at least one channel is ready for this peer.
  bool IsReady() const ABSL_LOCKS_EXCLUDED(mu_);

 private:
  enum class SetupState { kIdle, kSettingUp, kReady };

  // Tracks how many active channels to this peer are assigned to a worker.
  struct WorkerEntry {
    Worker* worker = nullptr;
    int count = 0;
  };

  // Resets setup state to idle and drains queued post handles.
  std::vector<Handle> drainAndResetLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  PeerWorkQueue work_queue_;
  mutable absl::Mutex mu_;
  SetupState state_ ABSL_GUARDED_BY(mu_) = SetupState::kIdle;
  int pending_connects_ ABSL_GUARDED_BY(mu_) = 0;
  int active_channels_ ABSL_GUARDED_BY(mu_) = 0;
  std::optional<SetupTrace> setup_trace_ ABSL_GUARDED_BY(mu_);
  absl::InlinedVector<WorkerEntry, 16> workers_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_
