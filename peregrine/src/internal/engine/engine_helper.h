#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/check.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/rdma/rdma_manager.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {

// Helper class for engine, which can focus on request scheduling.
// This class is thread-safe.
class EngineHelper final {
  using Channels = std::vector<std::unique_ptr<Channel>>;

 public:
  // Creates an engine helper and fills in the `self` host info.
  static std::unique_ptr<EngineHelper> Create(const Config& config,
                                              HostInfo& self, Control& control);

  // Disallows copy and move.
  DISALLOW_COPY(EngineHelper);
  DISALLOW_MOVE(EngineHelper);

  // Destructor.
  ~EngineHelper();

  // Returns a mutable reference to engine metrics.
  EngineMetrics& Metrics() { return metrics_; }

  // Returns a mutable reference to the trace recorder.
  TraceRecorder& Tracer() { return tracer_; }

  // Dynamically updates the trace configuration.
  void ConfigureTrace(const TraceConfig& config) { tracer_.Configure(config); }

  // Clears the trace buffer.
  void ClearTrace() { tracer_.Clear(); }

  // Takes a snapshot of assembled Post() trace records.
  std::vector<PostTraceRecord> GetTraceSnapshot() const;

  // Exports assembled Post() trace records in Chrome Trace Event JSON format.
  std::string ExportChromeTraceJson() const;

  // Creates a number of channels connected to the peer.
  Channels Connect(const Endpoint& peer_control, const TraceContext& trace_ctx,
                   Handle handle);

  // Returns the accepted channels.
  Channels GetAcceptedChannels();

  // Registers a contiguous memory buffer for rdma.
  absl::Status RegisterMemory(void* addr, size_t length);

  // Unregisters a previously registered rdma memory buffer.
  absl::Status UnregisterMemory(const void* addr);

 private:
  // Constructor.
  EngineHelper(const Config& config, const HostInfo& self, Control& control,
               std::unique_ptr<TcpManager> tcp_mgr,
               std::unique_ptr<RdmaManager> rdma_mgr);

  // Returns true if the following invariants hold.
  bool invariant() const {
    return config_.IsValid() && self_.IsDataPlaneValid() && tcp_mgr_ != nullptr;
  }

 private:
  // Creates `n` TCP channels that connect to the peer.
  Channels connectTcp(const Endpoint& peer_control, int n,
                      const TraceContext& trace_ctx, Handle handle);

  // Creates `n` RDMA channels that connect to the peer.
  Channels connectRdma(const Endpoint& peer_control, int n);

  // Returns an error if the config requires RDMA but its manager is null.
  absl::Status checkRdmaManager() const;

 private:
  const Config& config_;
  const HostInfo& self_;
  Control& control_;

  EngineMetrics metrics_;
  TraceRecorder tracer_;

  absl_nonnull std::unique_ptr<TcpManager> tcp_mgr_;
  absl_nullable std::unique_ptr<RdmaManager> rdma_mgr_;

  util::Jthread tcpmgr_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
