#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_RECORDER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_RECORDER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "absl/time/time.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/coding_style.h"
#include "peregrine/src/internal/trace/trace_buffer.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_sampler.h"
#include "peregrine/src/util/clock.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// Instrumentation facade used by TransportImpl, Engine, Worker, Transfer, and
// RequestTracker.
class TraceRecorder final {
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  explicit TraceRecorder(const TraceConfig& config)
      : sampler_(config.sample_every_n) {}

  DISALLOW_COPY(TraceRecorder);
  DISALLOW_MOVE(TraceRecorder);

  ~TraceRecorder() = default;

  // Dynamically updates the trace configuration.
  void Configure(const TraceConfig& config) noexcept;

  // Clears the ring buffer.
  void Clear() noexcept;

  // Returns the current monotonic timestamp in microseconds (always >= 1).
  static uint64_t NowUs() noexcept {
    const int64_t us = absl::ToInt64Microseconds(util::MonotonicNow());
    return static_cast<uint64_t>(std::max<int64_t>(1, us));
  }

  TraceSampler& Sampler() noexcept { return sampler_; }
  const TraceSampler& Sampler() const noexcept { return sampler_; }
  TraceBuffer& Buffer() noexcept { return buffer_; }
  const TraceBuffer& Buffer() const noexcept { return buffer_; }

  // Samples a new Post() invocation at entry.
  TraceContext MaybeStartTrace() noexcept;

  // Records T_PostEntry and T_EngineEnqueued once handle is allocated in
  // Engine::Enqueue().
  void RecordEngineEnqueued(const TraceContext& ctx, Handle handle,
                            uint64_t start_mono_us, size_t num_requests,
                            Op op) noexcept;

  // Records T_EngineDequeued on Engine::main_thread_.
  void RecordEngineDequeued(const TraceContext& ctx, Handle handle, ReqId reqid,
                            const Request& req) noexcept;

  // Records start of lazy connection setup (T_ConnSetupStart).
  void RecordConnSetupStart(const TraceContext& ctx, Handle handle,
                            int num_conns) noexcept;

  // Records completion of Control::GetPeerHostInfo (T_GetPeerHostInfoDone).
  void RecordGetPeerHostInfoDone(const TraceContext& ctx,
                                 Handle handle) noexcept;

  // Records T_ConnSetupDone on Engine::main_thread_.
  void RecordConnSetupDone(const TraceContext& ctx, Handle handle,
                           size_t num_workers) noexcept;

  // Records T_WorkerEnqueued for chunk index on Engine::main_thread_.
  void RecordWorkerEnqueued(const TraceContext& ctx, DataHeader& chunk,
                            int send_worker_id, Op op) noexcept;

  // Records T_WorkerDequeued and stamps chunk.trace.tx_send_mono_us.
  void RecordWorkerDequeued(DataHeader& chunk, int send_worker_id) noexcept;

  // Records T_TxWriteDone.
  void RecordTxWriteDone(const DataHeader& chunk, int send_worker_id) noexcept;

  // Records T_TxAckRcvd on sender's Worker::recv_thread_, unpacking
  // piggybacked receiver timings from chunk.trace.
  void RecordTxAckRcvd(const DataHeader& chunk) noexcept;

  // Records T_RxChunkDone locally on the receiver worker if receiver tracing
  // is enabled.
  void RecordRxChunkDone(const DataHeader& chunk, uint64_t t_done_us) noexcept;

  // Records T_AllChunksDone when the final chunk of the final ReqId completes
  // in RequestTracker::Update().
  void RecordAllChunksDone(const TraceContext& ctx, Handle handle, ReqId reqid,
                           chunk_t index, int worker_id) noexcept;

  // Records T_CallbackDone after on_complete finishes.
  void RecordCallbackDone(const TraceContext& ctx, Handle handle,
                          int worker_id) noexcept;

  // Records T_PollReaped when Poll(handle) reaps a completed handle.
  void RecordPollReaped(const TraceContext& ctx, Handle handle) noexcept;

 private:
  TraceSampler sampler_;
  TraceBuffer buffer_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_RECORDER_H_
