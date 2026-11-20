#include "peregrine/src/internal/engine2/peer_state.h"

#include <algorithm>
#include <memory>
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
    to_wake.insert(to_wake.end(), workers_.begin(), workers_.end());
  }
  return false;
}

void PeerState::OnConnectsStarted(const int count) {
  DCHECK_GE(count, 0);
  absl::MutexLock lock(mu_);
  pending_connects_ += count;
}

void PeerState::OnChannelReady(Worker* worker) {
  DCHECK(worker != nullptr);
  absl::MutexLock lock(mu_);
  state_ = SetupState::kReady;
  if (std::find(workers_.begin(), workers_.end(), worker) == workers_.end()) {
    workers_.push_back(worker);
  }
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
  if (state_ == SetupState::kReady) return {};
  return drainAndResetLocked();
}

void PeerState::Purge(const Handle handle) { work_queue_.Purge(handle); }

bool PeerState::IsReady() const {
  absl::MutexLock lock(mu_);
  return state_ == SetupState::kReady;
}

std::vector<Handle> PeerState::drainAndResetLocked() {
  state_ = SetupState::kIdle;
  pending_connects_ = 0;
  return work_queue_.DrainHandles();
}

}  // namespace peregrine::internal::engine2
