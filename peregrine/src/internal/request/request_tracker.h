#ifndef PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class tracks chunk departure/arrival for all the requests and handles.
// It is thread-safe.
class RequestTracker {
 public:
  using OnComplete = absl::AnyInvocable<void(Status)>;
  using OnRecvRead = absl::AnyInvocable<void(
      const Endpoint&, Handle, absl::Span<const RequestInfo>) const>;

  // Constructor.
  RequestTracker(EngineMetrics& metrics, TraceRecorder& tracer,
                 RecvPostCallback recv_post_callback = nullptr,
                 ResolveAddrCallback resolve_addr_callback = nullptr,
                 OnRecvRead on_recv_read = nullptr)
      : metrics_(metrics),
        tracer_(tracer),
        recv_post_callback_(std::move(recv_post_callback)),
        resolve_addr_callback_(std::move(resolve_addr_callback)),
        on_recv_read_(std::move(on_recv_read)) {}

  // Disable copy and move.
  DISALLOW_COPY(RequestTracker);
  DISALLOW_MOVE(RequestTracker);

  // Destructor.
  ~RequestTracker() = default;

  // Returns true iff there are no requests being tracked.
  bool IsEmpty() const ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Returns the status of the `handle`.
  Status Check(Handle handle) const ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Adds the tracker for the given `handle`.
  // Returns true iff the handle was not already in the tracker.
  bool Add(Handle handle, absl::Time start_time, OnComplete on_complete,
           const TraceContext& trace_ctx) ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Adds the `RequestInfo` descriptors (and optional post-level `metadata` and
  // sender `peer` endpoint) for `handle` (idempotent if already populated).
  bool Add(Handle handle, absl::Span<const RequestInfo> reqs,
           const Metadata& metadata = {}, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Removes the tracker for the given `handle`.
  void Remove(Handle handle) ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Updates pending request count when an incoming request in `handle`
  // completes all of its chunks, and invokes `recv_post_callback_` once all
  // requests in the `Post()` batch have completed.
  void OnReqComplete(Handle handle) ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Sets the chunk at `index` as completed for the given `handle` and `reqid`.
  // If this completes all chunks of all requests for the `handle`:
  // - If a completion callback is registered, it is invoked and the handle is
  //   automatically removed.
  // - Otherwise, the handle remains until `Poll()` is called.
  void Update(Handle handle, ReqId reqid, chunk_t index, int worker_id)
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Returns the target buffer address for a chunk at `byte_offset` within
  // `(handle, reqid)`, or `nullptr` if not found or unresolved.
  Byte* GetChunkAddr(Handle handle, ReqId reqid, uint64_t byte_offset) const
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Returns a reference to the chunk tracker for the given `handle` and
  // `reqid`.
  // TODO: Return `ChunkTracker*` (`nullptr` if `handle` or `reqid` is missing)
  // so callers avoid a prior `Check(handle)` call (which locks `trackers_mu_`
  // twice and scans `req_map` via `isComplete()`) and `.at()` aborts.
  ChunkTracker& Find(Handle handle, ReqId reqid)
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Finds a request tracker for the given `handle` and `reqid`.
  // If not found, creates a new one with the given `num_chunks`.
  // Returns a reference to the chunk tracker.
  ChunkTracker& FindOrCreate(Handle handle, ReqId reqid, uint32_t num_chunks)
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

 private:
  struct ReqTracker {
    ChunkTracker chunks;
    addr_t raddr = addr_t(0);
    Metadata metadata = {};

    explicit ReqTracker(uint32_t num_chunks, addr_t raddr = addr_t(0),
                        const Metadata& metadata = {})
        : chunks(num_chunks), raddr(raddr), metadata(metadata) {}
  };
  using ReqMap = absl::flat_hash_map<ReqId, std::unique_ptr<ReqTracker>>;

  struct PostTracker {
    ReqMap req_map;
    size_t pending_reqs = 0;
    Metadata metadata = {};
    absl::Time start_time;
    OnComplete on_complete;
    TraceContext trace_ctx;
    // True if initiated locally via `Post()`, false if received from a peer.
    bool is_initiator = false;

    // Default constructor.
    PostTracker() : start_time(kInvalidTime) {}

    // Constructor.
    PostTracker(ReqMap req_map, absl::Time start_time, OnComplete on_complete,
                const TraceContext& trace_ctx)
        : req_map(std::move(req_map)),
          start_time(start_time),
          on_complete(std::move(on_complete)),
          trace_ctx(trace_ctx),
          is_initiator(true) {}

    static constexpr absl::Time kInvalidTime = absl::InfinitePast();
  };

  // Returns true iff all the requests tracked by `pt` are complete.
  bool isComplete(const PostTracker& pt) const
      ABSL_SHARED_LOCKS_REQUIRED(trackers_mu_);

 private:
  EngineMetrics& metrics_;
  TraceRecorder& tracer_;

  mutable absl::Mutex trackers_mu_;
  absl::flat_hash_map<Handle, PostTracker> trackers_
      ABSL_GUARDED_BY(trackers_mu_);
  const RecvPostCallback recv_post_callback_;
  const ResolveAddrCallback resolve_addr_callback_;
  const OnRecvRead on_recv_read_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_
