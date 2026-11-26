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
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/lib/metric_counter.h"
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

  // Returns the status of `(peer, handle)`.
  Status Check(Handle handle, const Endpoint& peer = {}) const
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Adds the tracker for the given locally initiated `handle`.
  // Returns true iff the handle was not already in the tracker; `on_complete`
  // is only consumed when returning true.
  bool Add(Handle handle, uint64_t start_mono_us, OnComplete&& on_complete,
           const TraceContext& trace_ctx) ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Populates the `RequestInfo` descriptors for a locally initiated `handle`
  // previously registered via `Add(handle, start_mono_us, ...)` (keyed under
  // `Endpoint{}`). Kept separate from the remote `Add()` overload because
  // locally initiated requests (e.g., `Op::kRead`) supply trusted local
  // destination addresses in `req.raddr` without invoking
  // `resolve_addr_callback_`; keeping a dedicated entry point prevents remote
  // `PostInfoHeader` packets from colliding with an initiator handle and
  // injecting untrusted wire addresses. When `peer` is non-zero, incoming
  // `DataHeader` fallback lookups from identified peers must match `peer`.
  bool AddInitiatorReqs(Handle handle, absl::Span<const RequestInfo> reqs,
                        const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Adds the `RequestInfo` descriptors (and optional post-level `metadata`) for
  // an incoming remote `(peer, handle)` (idempotent if already populated).
  // Rejects locally initiated handles and unconditionally ignores wire
  // `req.raddr`, resolving and bounds-checking every target buffer via
  // `resolve_addr_callback_`.
  bool Add(Handle handle, absl::Span<const RequestInfo> reqs, uint64_t start_us,
           const Metadata& metadata = {}, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Removes the tracker for `(peer, handle)`.
  void Remove(Handle handle, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Fails the tracker for the given peer and handle. If a completion callback
  // is registered, invokes it with the failure status and removes the entry;
  // otherwise marks it failed so polling callers observe failure until removed.
  // When an error counter is provided, increments it atomically before making
  // the failure visible to callbacks or polling callers.
  bool Fail(Handle handle, Status status = Status::kFailure,
            const Endpoint& peer = {},
            MetricCounter<uint64_t>* error_counter = nullptr)
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Updates pending request count when an incoming request in `(peer, handle)`
  // completes all of its chunks, and invokes `recv_post_callback_` once all
  // requests in the `Post()` batch have completed. Returns true iff this call
  // completed the final pending request of the post.
  bool OnReqComplete(Handle handle, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Sets the chunk at `index` as completed for `(peer, handle, reqid)`.
  // If this completes all chunks of all requests for `(peer, handle)`:
  // - If a completion callback is registered, it is invoked and the handle is
  //   automatically removed.
  // - Otherwise, the handle remains until `Poll()` is called.
  void Update(Handle handle, ReqId reqid, chunk_t index, int worker_id,
              const Endpoint& peer = {}) ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Destination buffer address and chunk tracker returned by `FindChunk()`.
  struct ChunkTarget {
    Byte* dst = nullptr;
    ChunkTracker* tracker = nullptr;
  };

  // Looks up `(peer, handle, reqid)` under a single lock, returning the
  // destination address for `[byte_offset, byte_offset + chunk_len)` and its
  // `ChunkTracker`, or `{nullptr, nullptr}` if missing, unresolved, or out of
  // bounds.
  ChunkTarget FindChunk(Handle handle, ReqId reqid, uint64_t byte_offset,
                        uint64_t chunk_len, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Returns the target buffer address for a chunk of `chunk_len` bytes at
  // `byte_offset` within `(peer, handle, reqid)`, or `nullptr` if not found,
  // unresolved, or out of bounds of the request's `total_len`. If `out_tracker`
  // is non-null, `*out_tracker` is set to a shared reference to the request's
  // `ChunkTracker` on success (or `nullptr` on failure) under the same lock
  // acquisition so the caller can safely use it even if the `PostTracker` is
  // concurrently removed.
  Byte* GetChunkAddr(Handle handle, ReqId reqid, uint64_t byte_offset,
                     uint64_t chunk_len, const Endpoint& peer = {},
                     std::shared_ptr<ChunkTracker>* out_tracker = nullptr)
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Returns a reference to the chunk tracker for `(peer, handle, reqid)`.
  // TODO: Return `ChunkTracker*` (`nullptr` if `(peer, handle)` or `reqid` is
  // missing) so callers avoid a prior `Check()` call (which locks
  // `trackers_mu_` twice and scans `req_map` via `isComplete()`) and `.at()`
  // aborts.
  ChunkTracker& Find(Handle handle, ReqId reqid, const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

  // Finds a request tracker for `(peer, handle, reqid)`.
  // If not found, creates a new one with the given `num_chunks`.
  // Returns a reference to the chunk tracker.
  ChunkTracker& FindOrCreate(Handle handle, ReqId reqid, uint32_t num_chunks,
                             const Endpoint& peer = {})
      ABSL_LOCKS_EXCLUDED(trackers_mu_);

 private:
  using Key = std::pair<Endpoint, Handle>;

  struct ReqTracker {
    ChunkTracker chunks;
    addr_t raddr = addr_t(0);
    uint64_t total_len = 0;
    Metadata metadata = {};

    explicit ReqTracker(uint32_t num_chunks, addr_t raddr = addr_t(0),
                        uint64_t total_len = 0, const Metadata& metadata = {})
        : chunks(num_chunks),
          raddr(raddr),
          total_len(total_len),
          metadata(metadata) {}
  };
  using ReqMap = absl::flat_hash_map<ReqId, std::shared_ptr<ReqTracker>>;

  struct PostTracker {
    ReqMap req_map;
    size_t pending_reqs = 0;
    Metadata metadata = {};
    uint64_t start_mono_us = 0;
    OnComplete on_complete;
    TraceContext trace_ctx;
    // Target peer for locally initiated posts (when provided to
    // `AddInitiatorReqs`), used to reject `DataHeader` fallback lookups from
    // unrelated peers.
    Endpoint peer = {};
    // True if initiated locally via `Post()`, false if received from a peer.
    bool is_initiator = false;
    // True if this is a remote `Op::kRead` handled by the responder (must not
    // accept incoming `DataHeader` writes).
    bool is_responder_read = false;
    // True if address resolution failed on a received `PostInfoHeader`.
    bool failed = false;

    // Default constructor.
    PostTracker() = default;

    // Constructor.
    PostTracker(ReqMap req_map, uint64_t start_mono_us, OnComplete on_complete,
                const TraceContext& trace_ctx)
        : req_map(std::move(req_map)),
          start_mono_us(start_mono_us),
          on_complete(std::move(on_complete)),
          trace_ctx(trace_ctx),
          is_initiator(true) {}
  };

  // Returns true iff all the requests tracked by `pt` are complete.
  bool isComplete(const PostTracker& pt) const
      ABSL_SHARED_LOCKS_REQUIRED(trackers_mu_);

 private:
  EngineMetrics& metrics_;
  TraceRecorder& tracer_;

  mutable absl::Mutex trackers_mu_;
  absl::flat_hash_map<Key, PostTracker> trackers_ ABSL_GUARDED_BY(trackers_mu_);
  const RecvPostCallback recv_post_callback_;
  const ResolveAddrCallback resolve_addr_callback_;
  const OnRecvRead on_recv_read_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_REQUEST_REQUEST_TRACKER_H_
