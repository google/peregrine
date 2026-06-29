#include "src/internal/request/request_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal {

bool RequestTracker::IsEmpty() const {
  absl::MutexLock _(mu_);
  return trackers_.empty();
}

Status RequestTracker::Check(const Handle handle) const {
  absl::MutexLock _(mu_);
  const auto it = trackers_.find(handle);
  if ABSL_PREDICT_FALSE (it == trackers_.end()) {
    return Status::kNotFound;
  }
  if (it->second.empty()) {
    return Status::kInProgress;
  }
  for (const auto& [request, tracker] : it->second) {
    if (!tracker->IsDone()) return Status::kInProgress;
  }
  return Status::kSuccess;
}

bool RequestTracker::Add(const Handle handle) {
  absl::MutexLock _(mu_);
  return trackers_.try_emplace(handle, ReqMap()).second;
}

void RequestTracker::Remove(const Handle handle) {
  absl::MutexLock _(mu_);
  trackers_.erase(handle);
}

ChunkTracker* RequestTracker::FindOrCreate(const Handle handle,
                                           const ReqId reqid,
                                           const uint32_t num_chunks) {
  absl::MutexLock _(mu_);
  ReqMap& map = trackers_[handle];
  std::unique_ptr<ChunkTracker>& tracker = map[reqid];
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    tracker = std::make_unique<ChunkTracker>(num_chunks);
  }
  return tracker.get();
}

}  // namespace peregrine::internal
