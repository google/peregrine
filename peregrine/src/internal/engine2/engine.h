#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_ENGINE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_ENGINE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/transfer_manager.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Transport data plane accepting asynchronous transfer batches to remote peers.
//
// Thread-safety: This class is thread-safe.
class Engine final {
 public:
  // A batch of transport requests to one peer, submitted via `Enqueue()`.
  struct Item {
    absl::Span<const Request> requests;
    absl::AnyInvocable<void(Status)> on_complete;
    Metadata metadata = {};
    uint64_t start_mono_us = 0;
    TraceContext trace_ctx;

    // Returns true iff the item has at least one request and all are valid.
    bool IsValid() const;
  };

  // Creates an engine for `self` (which must outlive it). Populates
  // `self.data_plane_listeners`. Fails closed if `config` requests unsupported
  // features (such as dataplane encryption). Returns nullptr on failure.
  static std::unique_ptr<Engine> Create(
      const Config& config, HostInfo& self,
      GetPeerHostInfoFn get_peer_host_info_fn,
      RecvPostCallback recv_post_callback = nullptr,
      ResolveAddrCallback resolve_addr_callback = nullptr);

  DISALLOW_COPY(Engine);
  DISALLOW_MOVE(Engine);

  // Destroys the transfer manager before the trackers, metrics, and tracer.
  ~Engine() = default;

  // Submits `item` for transfer to `peer` and returns its handle without
  // blocking.
  absl::StatusOr<Handle> Enqueue(const PeerId& peer, Item item);

  // Returns the status of `handle`, and forgets `handle` once it is complete.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

  // Registers a contiguous memory buffer for RDMA (no-op for TCP).
  absl::Status RegisterMemory(void* addr, size_t length);

  // Unregisters a previously registered RDMA memory buffer (no-op for TCP).
  absl::Status UnregisterMemory(const void* addr);

  // Adds a snapshot of data-plane metrics to `m`.
  void GetMetrics(TransportMetrics& m) const { metrics_.Snapshot(m); }

  // Returns the trace recorder.
  TraceRecorder& Tracer() { return tracer_; }

  // Dynamically updates the trace configuration.
  void ConfigureTrace(const TraceConfig& config) { tracer_.Configure(config); }

  // Clears the trace buffer.
  void ClearTrace() { tracer_.Clear(); }

  // Exports buffered trace records in Chrome Trace Event JSON format.
  std::string ExportChromeTraceJson() const;

 private:
  Engine(const Config& config, const HostInfo& self,
         RecvPostCallback recv_post_callback,
         ResolveAddrCallback resolve_addr_callback);

 private:
  const Config config_;
  const HostInfo& self_;

  std::atomic<Handle::ValueType> next_handle_;
  std::atomic<ReqId::ValueType> next_req_id_;

  EngineMetrics metrics_;
  TraceRecorder tracer_;
  RequestTracker outgoing_;
  RequestTracker incoming_;

  // Declared last so it is destroyed before `outgoing_`, `incoming_`, and
  // `metrics_`.
  std::unique_ptr<TransferManager> transfer_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_ENGINE_H_
