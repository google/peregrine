#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_

#include <cstdint>

namespace peregrine::internal {

// Lifecycle stages recorded for a traced `Transport::Post()` operation.
enum class TraceStage : uint8_t {
  // Handle / Request batch level
  kPostEntry = 1,
  kEngineEnqueued = 2,
  kAllChunksDone = 3,
  kCallbackDone = 4,
  kPollInProgress = 5,
  kPollReaped = 6,

  // Per-Request (ReqId) level on Engine::main_thread_
  kEngineDequeued = 20,
  kConnSetupStart = 21,
  kGetPeerHostInfoDone = 22,
  kChannelConnectDone = 23,
  kConnSetupDone = 24,
  kWorkerEnqueued = 25,

  // Per-Chunk level on Sender & Receiver Workers
  kWorkerDequeued = 40,
  kTxWriteDone = 41,
  kRdmaPostSend = 42,
  kRdmaCqeReaped = 43,
  kTxAckRcvd = 44,    // Also records piggybacked rx_acquire_us / rx_payload_us
  kRxChunkDone = 45,  // Recorded locally on receiver if receiver tracing is on
};

// Fixed-size 40-byte trace record stored in `TraceBuffer`.
struct TraceEvent final {
  uint64_t timestamp_us = 0;  // util::MonotonicNow() in microseconds
  uint32_t handle = 0;
  uint32_t reqid = 0;
  uint32_t chunk_index = 0;
  uint32_t duration_us =
      0;  // Stage duration in us (e.g. chunk_rtt_us or tx_write_us)
  uint32_t aux_value =
      0;  // Secondary metric (e.g. rx_payload_us or size_bytes)
  uint32_t aux2_value = 0;  // Tertiary metric (e.g. rx_acquire_us)
  int16_t worker_id =
      0;  // Worker ID (+k for send, -k for recv, 0 for engine/api)
  TraceStage stage = TraceStage::kPostEntry;  // Lifecycle stage enum
  uint8_t flags = 0;  // Extra metadata (e.g., ChunkStatus, Op, TransportType)
};
static_assert(sizeof(TraceEvent) == 40);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_
