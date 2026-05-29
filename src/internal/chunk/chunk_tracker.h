#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/util/util.h"

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
//    entrance >  EMPTY
//         v        |
//                  V
//       ERROR --> DONE      (w/ a BUSY flag)
//
// The entrance is either EMPTY or ERROR. DONE is final so it can only be
// updated at most once. When a writer is busy writing the chunk data, the
// BUSY flag for the chunk is set. It is cleared after the chunk data writing
// is completed, with the state updated to either DONE (for success) or ERROR
// (for failure).
//
// This class is thread-safe.
class ChunkTracker {
 public:
  // Constructs a tracker with `total_num_chunks` chunk states.
  explicit ChunkTracker(uint32_t total_num_chunks);

  // Disallows copy/move constructors and assignment operators.
  ChunkTracker(const ChunkTracker&) = delete;
  ChunkTracker& operator=(const ChunkTracker&) = delete;
  ChunkTracker(ChunkTracker&&) = delete;
  ChunkTracker& operator=(ChunkTracker&&) = delete;

  // Destructor.
  ~ChunkTracker() = default;

  // Returns the total number of chunks.
  uint32_t TotalNumChunks() const { return total_num_chunks_; }

  // Returns true iff the `index`-th chunk is being busy written.
  bool IsBusy(chunk_t index) const ABSL_LOCKS_EXCLUDED(mu_);

  // Gets the exclusive data write access to the `index`-th chunk.
  // Returns true if the permission is granted.
  bool Acquire(chunk_t index) ABSL_LOCKS_EXCLUDED(mu_);

  // Releases the exclusive data write access to the `index`-th chunk.
  // Returns true iff all the chunks have been written successfully.
  // PRECONDITION: The caller must have called Acquire() and it returned true.
  bool Release(chunk_t index, bool success) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns true iff no chunk has been written yet.
  bool IsEmpty() const ABSL_LOCKS_EXCLUDED(mu_);

  // Returns true iff all the chunks have been written successfully.
  bool IsCompleted() const ABSL_LOCKS_EXCLUDED(mu_);

  // Returns a string representation of the tracker.
  std::string ToString() const ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Chunk states. Do not change the values.
  using ChunkState = uint8_t;
  static constexpr ChunkState kChunkEmpty = 0x00;  // Never been written.
  static constexpr ChunkState kChunkError = 0x01;  // Written but failed.
  static constexpr ChunkState kChunkDone = 0x02;   // Data has been filled.
  static constexpr ChunkState kChunkStateMask = 0x03;
  static constexpr ChunkState kChunkBusyFlag = 0x10;  // It is being written.
  static_assert(IsPowerOfTwo<uint8_t>(kChunkStateMask + 1));
  static_assert((kChunkBusyFlag & kChunkStateMask) == 0);

 private:
  // Returns true iff the chunk state has the BUSY flag set.
  constexpr bool testBusyBit(ChunkState s) const {
    return (s & kChunkBusyFlag) != 0;
  }

  // Returns true iff the chunk index is valid.
  bool isValidChunk(chunk_t index) const {
    return 0 <= index.value() && index.value() < total_num_chunks_;
  }

  // Returns true iff the invariant holds.
  bool invariant() const ABSL_SHARED_LOCKS_REQUIRED(mu_);

 private:
  const uint32_t total_num_chunks_;

  mutable absl::Mutex mu_;
  uint32_t num_chunks_done_ ABSL_GUARDED_BY(mu_);  // cache
  uint32_t num_chunks_dup_ ABSL_GUARDED_BY(mu_);
  std::vector<ChunkState> states_ ABSL_GUARDED_BY(mu_);
};

inline std::ostream& operator<<(std::ostream& os, const ChunkTracker& t) {
  return os << t.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TRACKER_H_
