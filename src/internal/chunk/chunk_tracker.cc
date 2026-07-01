#include "src/internal/chunk/chunk_tracker.h"

#include <cstdint>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

ChunkTracker::ChunkTracker(const uint32_t total_num_chunks)
    : total_num_chunks_(total_num_chunks),
      num_chunks_dup_(0),
      chunks_(total_num_chunks_) {
  DCHECK(invariant());
  DCHECK_GE(total_num_chunks_, 1);
  busy_chunks_.reserve(64);  // TODO(yongx): tune this value.
}

void ChunkTracker::Set(const chunk_t index) {
  absl::MutexLock lock(mu_);
  DCHECK(isValidChunk(index));
  DCHECK(!chunks_.Get(index.value()));
  DCHECK(!busy_chunks_.contains(index));
  chunks_.Set(index.value());
}

bool ChunkTracker::Acquire(const chunk_t index) {
  DCHECK(isValidChunk(index));

  absl::MutexLock lock(mu_);
  DCHECK(invariant());
  if ABSL_PREDICT_FALSE (chunks_.Get(index.value())) {
    return false;
  }
  if ABSL_PREDICT_FALSE (!busy_chunks_.insert(index).second) {
    ++num_chunks_dup_;
    return false;
  }
  return true;
}

void ChunkTracker::Release(const chunk_t index, bool success) {
  DCHECK(isValidChunk(index));

  absl::MutexLock lock(mu_);
  DCHECK(invariant());
  DCHECK(busy_chunks_.contains(index));
  DCHECK(!chunks_.Get(index.value()));
  if ABSL_PREDICT_TRUE (success) {
    chunks_.Set(index.value());
  }
  busy_chunks_.erase(index);
}

std::string ChunkTracker::ToString() const {
  absl::MutexLock lock(mu_);
  DCHECK(invariant());
  return absl::StrFormat("Chunks(done/dup/total)=%d/%d/%d", chunks_.Count(),
                         num_chunks_dup_, chunks_.Size());
}

}  // namespace peregrine::internal
