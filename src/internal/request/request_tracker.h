#ifndef PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_

#include <cstdint>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/metrics/engine_metrics.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class tracks chunk departure/arrival for all the requests and handles.
// It is thread-safe.
class RequestTracker {
 public:
  using OnCompleteCallback = absl::AnyInvocable<void(Status)>;

  // Constructor.
  explicit RequestTracker(EngineMetrics& metrics) : metrics_(metrics) {}

  // Disable copy and move.
  DISALLOW_COPY(RequestTracker);
  DISALLOW_MOVE(RequestTracker);

  // Destructor.
  ~RequestTracker() = default;

  // Returns true iff there are no requests being tracked.
  bool IsEmpty() const ABSL_LOCKS_EXCLUDED(mu_);

  // Returns the status of the `handle`.
  Status Check(Handle handle) const ABSL_LOCKS_EXCLUDED(mu_);

  // Adds the tracker for the given `handle` and `reqids`.
  // Returns true iff the handle was not already in the tracker.
  bool Add(Handle handle, absl::Span<const ReqId> reqids,
           OnCompleteCallback on_complete) ABSL_LOCKS_EXCLUDED(mu_);

  // Removes the tracker for the given `handle`.
  void Remove(Handle handle) ABSL_LOCKS_EXCLUDED(mu_);

  // Sets the chunk at `index` as completed for the given `handle` and `reqid`.
  // If this completes all chunks of all requests for the `handle`:
  // - If a completion callback is registered, it is invoked and the handle is
  //   automatically removed.
  // - Otherwise, the handle remains until `Poll()` is called.
  void Set(Handle handle, ReqId reqid, uint32_t num_chunks, chunk_t index)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Finds a request tracker for the given `handle` and `reqid`.
  // If not found, creates a new one with the given `num_chunks`.
  // Returns a reference to the chunk tracker.
  ChunkTracker& FindOrCreate(Handle handle, ReqId reqid, uint32_t num_chunks)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  using ReqMap = absl::flat_hash_map<ReqId, std::unique_ptr<ChunkTracker>>;

  struct ReqsTracker {
    ReqMap req_map;
    OnCompleteCallback on_complete;
    absl::Time start_time = absl::InfinitePast();
  };

  // Returns true iff all requests tracked by `rt` are complete.
  bool isComplete(const ReqsTracker& rt) const ABSL_SHARED_LOCKS_REQUIRED(mu_);

 private:
  EngineMetrics& metrics_;

  mutable absl::Mutex mu_;
  absl::flat_hash_map<Handle, ReqsTracker> trackers_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_
