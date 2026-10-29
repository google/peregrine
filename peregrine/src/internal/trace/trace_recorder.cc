#include "peregrine/src/internal/trace/trace_recorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "absl/base/optimization.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/trace/trace_buffer.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine::internal {

namespace {

// Records a connection-setup stage event.
void RecordConnSetupStage(TraceBuffer& buffer, const TraceContext& ctx,
                          Handle handle, uint32_t metric1,
                          TraceStage stage) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  buffer.Record(TraceEvent{
      .timestamp_us = TraceRecorder::NowUs(),
      .handle = handle.value(),
      .stage = stage,
      .metric1 = metric1,
  });
}

// Records a sender worker chunk stage event.
void RecordSendChunkStage(TraceBuffer& buffer, const DataHeader& chunk,
                          int send_worker_id, uint64_t timestamp_us,
                          TraceStage stage) noexcept {
  buffer.Record(TraceEvent{
      .timestamp_us = timestamp_us,
      .handle = chunk.handle.value(),
      .reqid = chunk.reqid.value(),
      .stage = stage,
      .worker_id = ClampI16(send_worker_id),
      .chunk_idx = chunk.index.value(),
      .metric1 = chunk.len,
  });
}

// Records a request completion stage event.
void RecordCompletionStage(TraceBuffer& buffer, const TraceContext& ctx,
                           Handle handle, ReqId reqid, chunk_t index,
                           int worker_id, TraceStage stage) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  buffer.Record(TraceEvent{
      .timestamp_us = TraceRecorder::NowUs(),
      .handle = handle.value(),
      .reqid = reqid.value(),
      .stage = stage,
      .worker_id = ClampI16(worker_id),
      .chunk_idx = index.value(),
  });
}

}  // namespace

void TraceRecorder::Configure(const TraceConfig& config) noexcept {
  sampler_.SetSampleEveryN(config.sample_every_n);
}

void TraceRecorder::Clear() noexcept { buffer_.Clear(); }

TraceContext TraceRecorder::MaybeStartTrace() noexcept {
  if ABSL_PREDICT_TRUE (!sampler_.ShouldSample()) {
    return TraceContext();
  }
  return TraceContext(/*is_traced=*/true);
}

void TraceRecorder::RecordEngineEnqueued(const TraceContext& ctx, Handle handle,
                                         uint64_t start_mono_us,
                                         size_t num_requests, Op op) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  const uint64_t t_enqueued_us = NowUs();
  // Handle is generated inside Engine::Enqueue(), after Post() captures
  // start_mono_us at API entry. Emitting kPostEntry here where both
  // kPostEntry and kEngineEnqueued can be tagged with handle.
  buffer_.Record(TraceEvent{
      .timestamp_us = start_mono_us,
      .handle = handle.value(),
      .stage = TraceStage::kPostEntry,
      .metadata = static_cast<uint8_t>(op),
      .metric1 = ClampU32(num_requests),
  });
  buffer_.Record(TraceEvent{
      .timestamp_us = t_enqueued_us,
      .handle = handle.value(),
      .stage = TraceStage::kEngineEnqueued,
      .metadata = static_cast<uint8_t>(op),
      .metric1 = ClampU32(num_requests),
  });
}

void TraceRecorder::RecordEngineDequeued(const TraceContext& ctx, Handle handle,
                                         ReqId reqid,
                                         const Request& req) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  buffer_.Record(TraceEvent{
      .timestamp_us = NowUs(),
      .handle = handle.value(),
      .reqid = reqid.value(),
      .stage = TraceStage::kEngineDequeued,
      .metadata = static_cast<uint8_t>(req.op),
      .metric1 = ClampU32(req.len),
  });
}

void TraceRecorder::RecordConnSetupStart(const TraceContext& ctx, Handle handle,
                                         int num_conns) noexcept {
  RecordConnSetupStage(buffer_, ctx, handle,
                       static_cast<uint32_t>(std::max(0, num_conns)),
                       TraceStage::kConnSetupStart);
}

void TraceRecorder::RecordGetPeerHostInfoDone(const TraceContext& ctx,
                                              Handle handle) noexcept {
  RecordConnSetupStage(buffer_, ctx, handle, /*metric1=*/0,
                       TraceStage::kGetPeerHostInfoDone);
}

void TraceRecorder::RecordConnSetupDone(const TraceContext& ctx, Handle handle,
                                        size_t num_workers) noexcept {
  RecordConnSetupStage(buffer_, ctx, handle, ClampU32(num_workers),
                       TraceStage::kConnSetupDone);
}

void TraceRecorder::RecordWorkerEnqueued(const DataHeader& chunk,
                                         int send_worker_id) noexcept {
  if ABSL_PREDICT_TRUE (!chunk.trace.IsTraced()) return;
  RecordSendChunkStage(buffer_, chunk, send_worker_id, NowUs(),
                       TraceStage::kWorkerEnqueued);
}

void TraceRecorder::RecordWorkerDequeued(DataHeader& chunk,
                                         int send_worker_id) noexcept {
  if ABSL_PREDICT_TRUE (!chunk.trace.IsTraced()) return;
  const uint64_t t_dequeued_us = NowUs();
  chunk.trace.tx_send_mono_us = t_dequeued_us;
  RecordSendChunkStage(buffer_, chunk, send_worker_id, t_dequeued_us,
                       TraceStage::kWorkerDequeued);
}

void TraceRecorder::RecordTxWriteDone(const DataHeader& chunk,
                                      int send_worker_id) noexcept {
  if ABSL_PREDICT_TRUE (!chunk.trace.IsTraced()) return;
  RecordSendChunkStage(buffer_, chunk, send_worker_id, NowUs(),
                       TraceStage::kTxWriteDone);
}

void TraceRecorder::RecordTxAckRcvd(const DataHeader& chunk) noexcept {
  if ABSL_PREDICT_TRUE (!chunk.trace.IsTraced()) return;
  buffer_.Record(TraceEvent{
      .timestamp_us = NowUs(),
      .handle = chunk.handle.value(),
      .reqid = chunk.reqid.value(),
      .stage = TraceStage::kTxAckRcvd,
      .metadata = chunk.trace.rx_chunk_status,
      .worker_id = static_cast<int16_t>(chunk.trace.rx_worker_id),
      .chunk_idx = chunk.index.value(),
      .metric1 = chunk.trace.rx_payload_us,
      .metric2 = chunk.trace.rx_acquire_us,
  });
}

void TraceRecorder::RecordRequestDone(const TraceContext& ctx, Handle handle,
                                      ReqId reqid, chunk_t index,
                                      int worker_id) noexcept {
  RecordCompletionStage(buffer_, ctx, handle, reqid, index, worker_id,
                        TraceStage::kRequestDone);
}

void TraceRecorder::RecordAllRequestsDone(const TraceContext& ctx,
                                          Handle handle, ReqId reqid,
                                          chunk_t index,
                                          int worker_id) noexcept {
  RecordCompletionStage(buffer_, ctx, handle, reqid, index, worker_id,
                        TraceStage::kAllRequestsDone);
}

void TraceRecorder::RecordCallbackDone(const TraceContext& ctx, Handle handle,
                                       int worker_id) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  buffer_.Record(TraceEvent{
      .timestamp_us = NowUs(),
      .handle = handle.value(),
      .stage = TraceStage::kCallbackDone,
      .worker_id = ClampI16(worker_id),
  });
}

void TraceRecorder::RecordPollReaped(const TraceContext& ctx,
                                     Handle handle) noexcept {
  if ABSL_PREDICT_TRUE (!ctx.is_traced) return;
  buffer_.Record(TraceEvent{
      .timestamp_us = NowUs(),
      .handle = handle.value(),
      .stage = TraceStage::kPollReaped,
  });
}

}  // namespace peregrine::internal
