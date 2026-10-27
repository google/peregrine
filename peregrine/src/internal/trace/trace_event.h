#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_

#include <cstdint>
#include <limits>

namespace peregrine::internal {

// Lifecycle stages recorded for a traced `Transport::Post()` operation.
enum class TraceStage : uint8_t {
  // Handle / Request batch level
  kPostEntry = 1,
  kEngineEnqueued = 2,
  kAllChunksDone = 3,
  kCallbackDone = 4,
  kPollReaped = 5,

  // Per-Request (ReqId) level on Engine::main_thread_
  kEngineDequeued = 20,
  kConnSetupStart = 21,
  kGetPeerHostInfoDone = 22,
  kConnSetupDone = 23,
  kWorkerEnqueued = 24,

  // Per-Chunk level on Sender & Receiver Workers
  kWorkerDequeued = 40,
  kTxWriteDone = 41,
  kRdmaPostSend = 42,
  kRdmaCqeReaped = 43,
  kTxAckRcvd = 44,  // Also records piggybacked rx_acquire_us / rx_payload_us
};

// Fixed-size 32-byte trace record stored in `TraceBuffer`.
struct TraceEvent final {
  static constexpr uint32_t kNoChunk = std::numeric_limits<uint32_t>::max();

  uint64_t timestamp_us = 0;  // util::MonotonicNow() in microseconds
  uint32_t handle = 0;
  uint32_t reqid = 0;
  TraceStage stage = TraceStage::kPostEntry;  // Lifecycle stage enum
  uint8_t metadata = 0;  // Extra metadata (e.g. ChunkStatus, Op, TransportType)
  int16_t worker_id =
      0;  // Worker ID (+k for send, -k for recv, 0 for engine/api)
  uint32_t chunk_idx = kNoChunk;
  uint32_t metric1 = 0;  // E.g. rx_payload_us or size_bytes
  uint32_t metric2 = 0;  // E.g. rx_acquire_us
};
static_assert(sizeof(TraceEvent) == 32);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_EVENT_H_
