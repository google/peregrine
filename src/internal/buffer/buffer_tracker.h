#ifndef PEREGRINE_SRC_INTERNAL_BUFFER_BUFFER_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_BUFFER_BUFFER_TRACKER_H_

#include <cstdint>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class tracks chunk departure/arrival for all the buffers and handles.
// It is thread-safe.
class BufferTracker {
 public:
  // Constructor.
  BufferTracker() = default;

  // Disable copy and move.
  DISALLOW_COPY(BufferTracker);
  DISALLOW_MOVE(BufferTracker);

  // Destructor.
  ~BufferTracker() = default;

  // Returns true iff there are no buffers being tracked.
  bool IsEmpty() const ABSL_LOCKS_EXCLUDED(mu_);

  // Returns the status of the `handle`.
  Status Check(Handle handle) const ABSL_LOCKS_EXCLUDED(mu_);

  // Adds the tracker for the given `handle`.
  // Returns true iff the handle was not already in the tracker.
  bool Add(Handle handle) ABSL_LOCKS_EXCLUDED(mu_);

  // Removes the tracker for the given `handle`.
  void Remove(Handle handle) ABSL_LOCKS_EXCLUDED(mu_);

  // Finds a buffer tracker for the given `handle` and `buffer`.
  // If not found, creates a new one with the given `num_chunks`.
  // Returns the (always non-null) tracker pointer.
  ChunkTracker* FindOrCreate(Handle handle, Buffer buffer, uint32_t num_chunks)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  using BufferMap = absl::flat_hash_map<Buffer, std::unique_ptr<ChunkTracker>>;

 private:
  mutable absl::Mutex mu_;
  absl::flat_hash_map<Handle, BufferMap> trackers_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BUFFER_BUFFER_TRACKER_H_
