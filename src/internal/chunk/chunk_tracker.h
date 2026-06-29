#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/lib/bitset.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class tracks the chunks of a buffer, which is transferred from its
// source (host/device memory) to its destination (device/host memory).
// Chunk data writing is exclusive: at any time, for a single chunk, there
// can be at most one writer busy writing the chunk data. However,
// multiple writers can write different chunks at the same time.
//
// We assume that the contention on the same chunk is low: it is rare for
// a set of communication channels to repeatedly send the same chunk data.
//
// Chunk state machine:
//
//             Acquire()           Release(..., success=true)
//    EMPTY <-------------> BUSY -----------------------------> DONE
//      ^                    |
//      |                    |  Release(..., success=false)
//      +--------------------+
//
// EMPTY is the initial state. To write a chunk, a writer must first call
// Acquire() to get exclusive write access, which transitions the chunk's
// state from EMPTY to BUSY. If Acquire() returns false, the writer must
// not write to the chunk. Note that Acquire() only returns false when the
// chunk is already BUSY or DONE.
//
// Once the writer finishes writing, it must call Release(). If the write
// succeeded, the chunk's state transitions to DONE (which is final). If
// the write failed, the chunk's state transitions back to EMPTY so that
// other writers can attempt to write the chunk again.
//
// This class is thread-safe.
class ChunkTracker final {
 public:
  // Constructs a tracker with `total_num_chunks` chunk states.
  explicit ChunkTracker(uint32_t total_num_chunks);

  // Disables copy/move.
  DISALLOW_COPY(ChunkTracker);
  DISALLOW_MOVE(ChunkTracker);

  // Destructor.
  ~ChunkTracker() = default;

  // Returns the total number of chunks.
  uint32_t TotalNumChunks() const { return total_num_chunks_; }

  // Returns true iff no chunk has been written yet.
  bool IsEmpty() const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return chunks_.IsEmpty();
  }

  // Returns true iff all the chunks have been written successfully.
  bool IsDone() const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return chunks_.IsFull();
  }

  // Returns true iff the `index`-th chunk is being busy written.
  bool IsBusy(chunk_t index) const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return busy_chunks_.contains(index);
  }

  // Sets the `index`-th chunk to indicate it has been read.
  void Set(chunk_t index) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    DCHECK(isValidChunk(index));
    DCHECK(!chunks_.Get(index.value()));
    DCHECK(!busy_chunks_.contains(index));
    chunks_.Set(index.value());
  }

  // Gets the exclusive data write access to the `index`-th chunk.
  // Returns true if the permission is granted.
  bool Acquire(chunk_t index) ABSL_LOCKS_EXCLUDED(mu_);

  // Releases the exclusive data write access to the `index`-th chunk.
  // PRECONDITION: The caller must have called Acquire() and it returned true.
  void Release(chunk_t index, bool success) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns a string representation of the tracker.
  std::string ToString() const ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Returns true iff the chunk index is valid.
  bool isValidChunk(chunk_t index) const {
    return 0 <= index.value() && index.value() < total_num_chunks_;
  }

  // Returns true iff the invariant holds.
  bool invariant() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
    return chunks_.Size() == total_num_chunks_;
  }

 private:
  const uint32_t total_num_chunks_;  // cache

  mutable absl::Mutex mu_;
  uint32_t num_chunks_dup_ ABSL_GUARDED_BY(mu_);
  Bitset chunks_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_set<chunk_t> busy_chunks_ ABSL_GUARDED_BY(mu_);
};

inline std::ostream& operator<<(std::ostream& os, const ChunkTracker& t) {
  return os << t.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_
