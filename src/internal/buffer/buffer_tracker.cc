#include "src/internal/buffer/buffer_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/chunk/tracker.h"

namespace peregrine::internal {

bool BufferTracker::IsEmpty() const {
  absl::MutexLock _(mu_);
  return trackers_.empty();
}

bool BufferTracker::IsDone(const Handle handle) const {
  absl::MutexLock _(mu_);
  const auto it = trackers_.find(handle);
  if ABSL_PREDICT_FALSE (it == trackers_.end()) {
    return false;
  }
  for (const auto& [buffer, tracker] : it->second) {
    if (!tracker->IsDone()) return false;
  }
  return true;
}

Tracker* BufferTracker::FindOrCreate(const Handle handle, const Buffer buffer,
                                     const uint32_t num_chunks) {
  absl::MutexLock _(mu_);
  BufferMap& buffer_map = trackers_[handle];
  std::unique_ptr<Tracker>& tracker = buffer_map[buffer];
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    tracker = std::make_unique<ChunkTracker>(num_chunks);
  }
  return tracker.get();
}

void BufferTracker::Remove(const Handle handle) {
  absl::MutexLock _(mu_);
  trackers_.erase(handle);
}

}  // namespace peregrine::internal
