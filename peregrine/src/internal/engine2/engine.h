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
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/transfer_manager.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// The transport data plane.
//
// `Engine` accepts batches of transport requests to remote peers, transfers
// them asynchronously, and reports each batch's outcome through its
// `on_complete` callback or through `QueryUpdate()`. It also serves requests
// initiated by remote peers.
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

  // Creates an engine for the local host described by `self`, which must
  // outlive the engine. `get_peer_host_info_fn` looks up remote peers.
  // `recv_post_callback` and `resolve_addr_callback` serve requests initiated
  // by remote peers. Returns nullptr on failure.
  //
  // Implementation details:
  // - If `self` has no NICs, discovers them into `self.tcp_nics` and
  //   `self.rdma_nics`.
  // - Creates the outgoing and incoming request trackers. The incoming tracker
  //   gets `recv_post_callback` and `resolve_addr_callback`, and reports
  //   incoming two-sided reads so the engine submits the write-back. The
  //   outgoing tracker reports posts rejected by the peer so the engine calls
  //   `TransferManager::Cancel()`.
  // - Creates the channel manager, which sets up the transports enabled by
  //   `config` and records their endpoints in `self.data_plane_listeners`.
  // - Creates the transfer manager with the channel manager and
  //   `get_peer_host_info_fn`; the transfer manager starts accepting channels.
  static std::unique_ptr<Engine> Create(
      const Config& config, HostInfo& self,
      GetPeerHostInfoFn get_peer_host_info_fn,
      RecvPostCallback recv_post_callback = nullptr,
      ResolveAddrCallback resolve_addr_callback = nullptr);

  DISALLOW_COPY(Engine);
  DISALLOW_MOVE(Engine);

  // Implementation details:
  // - Destroys the transfer manager before the request trackers, metrics, and
  //   tracer it uses.
  ~Engine() = default;

  // Submits `item` for transfer to `peer` and returns its handle. Does not wait
  // for the transfer or for a connection to `peer`. `peer` must be a valid
  // endpoint and `item` must be valid.
  //
  // Implementation details:
  // - Registers the batch in the outgoing tracker (writes) or the incoming
  //   tracker (reads), together with `item.on_complete`.
  absl::StatusOr<Handle> Enqueue(const PeerId& peer, Item item);

  // Returns the status of `handle`, and forgets `handle` once it is complete.
  //
  // Implementation details:
  // - Looks up `handle` in the outgoing tracker, then the incoming tracker.
  // - Returns `NotFoundError` if neither tracks it; removes it once complete.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

  // Registers a contiguous memory buffer for RDMA.
  //
  // Implementation details:
  // - Returns OK until the RDMA transport exists; then delegates to it.
  absl::Status RegisterMemory(void* addr, size_t length);

  // Unregisters a previously registered RDMA memory buffer.
  //
  // Implementation details:
  // - Returns OK until the RDMA transport exists; then delegates to it.
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
         std::unique_ptr<TransferManager> transfer_manager);

 private:
  const Config config_;
  const HostInfo& self_;

  std::atomic<Handle::ValueType> next_handle_;

  EngineMetrics metrics_;
  TraceRecorder tracer_;

  // Declared last so it is destroyed first.
  std::unique_ptr<TransferManager> transfer_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_ENGINE_H_
