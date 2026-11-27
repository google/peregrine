#include "peregrine/src/internal/engine2/peer_state.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/worker.h"

namespace peregrine::internal::engine2 {

bool PeerState::Enqueue(std::shared_ptr<QueuedPost> post, WorkerList& to_wake) {
  absl::MutexLock lock(mu_);
  work_queue_.Enqueue(std::move(post));
  if (state_ == SetupState::kIdle) {
    state_ = SetupState::kSettingUp;
    return true;
  }
  if (state_ == SetupState::kReady) {
    for (const WorkerEntry& entry : workers_) {
      to_wake.push_back(entry.worker);
    }
  }
  return false;
}

bool PeerState::TryBeginSetup(const SetupTrace& trace) {
  absl::MutexLock lock(mu_);
  if (state_ != SetupState::kSettingUp || pending_connects_ > 0) return false;
  if (trace.trace_ctx.is_traced) setup_trace_ = trace;
  return true;
}

bool PeerState::OnConnectsStarted(const int count) {
  DCHECK_GE(count, 0);
  absl::MutexLock lock(mu_);
  if (state_ != SetupState::kSettingUp) return false;
  pending_connects_ += count;
  return true;
}

std::optional<PeerState::SetupTrace> PeerState::OnChannelReady(
    Worker* worker, const bool is_outbound) {
  DCHECK(worker != nullptr);
  absl::MutexLock lock(mu_);
  if (is_outbound && pending_connects_ > 0) --pending_connects_;
  ++active_channels_;
  state_ = SetupState::kReady;
  const auto it = std::find_if(
      workers_.begin(), workers_.end(),
      [worker](const WorkerEntry& entry) { return entry.worker == worker; });
  if (it == workers_.end()) {
    workers_.push_back(WorkerEntry{.worker = worker, .count = 1});
  } else {
    ++it->count;
  }
  return std::exchange(setup_trace_, std::nullopt);
}

std::vector<Handle> PeerState::OnChannelClosed(Worker* worker) {
  DCHECK(worker != nullptr);
  absl::MutexLock lock(mu_);
  const auto it = std::find_if(
      workers_.begin(), workers_.end(),
      [worker](const WorkerEntry& entry) { return entry.worker == worker; });
  if (it == workers_.end()) {
    DCHECK(false) << "closing untracked worker channel";
    return {};
  }
  if (--it->count == 0) {
    workers_.erase(it);
  }
  DCHECK_GT(active_channels_, 0);
  if (--active_channels_ > 0) return {};
  if (pending_connects_ == 0) {
    return drainAndResetLocked();
  }
  state_ = SetupState::kSettingUp;
  return {};
}

std::vector<Handle> PeerState::OnConnectFailed() {
  static_assert(assumptions::kSetupFailuresAbortQueuedPosts);
  absl::MutexLock lock(mu_);
  if (pending_connects_ > 0) --pending_connects_;
  if (pending_connects_ == 0 && state_ != SetupState::kReady) {
    return drainAndResetLocked();
  }
  return {};
}

std::vector<Handle> PeerState::AbortSetup() {
  static_assert(assumptions::kSetupFailuresAbortQueuedPosts);
  absl::MutexLock lock(mu_);
  if (state_ != SetupState::kSettingUp || pending_connects_ > 0) return {};
  return drainAndResetLocked();
}

void PeerState::Purge(const Handle handle) { work_queue_.Purge(handle); }

bool PeerState::IsReady() const {
  absl::MutexLock lock(mu_);
  return state_ == SetupState::kReady;
}

std::vector<Handle> PeerState::drainAndResetLocked() {
  DCHECK_EQ(active_channels_, 0);
  DCHECK(workers_.empty());
  state_ = SetupState::kIdle;
  pending_connects_ = 0;
  setup_trace_.reset();
  return work_queue_.DrainHandles();
}

}  // namespace peregrine::internal::engine2
