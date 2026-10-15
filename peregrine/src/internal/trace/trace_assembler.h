#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_ASSEMBLER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_ASSEMBLER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

// Stage timestamps of a traced chunk, taken from TraceRecorder::NowUs(),
// or 0 if a stage was not captured.
struct ChunkTimeline final {
  uint32_t index = 0;
  uint32_t size_bytes = 0;
  int16_t send_worker_id = 0;
  int16_t recv_worker_id = 0;
  uint64_t t_enqueued_us = 0;  // T_WorkerEnqueued
  uint64_t t_dequeued_us = 0;  // T_WorkerDequeued
  uint64_t t_tx_done_us = 0;   // T_TxWriteDone
  uint64_t t_ack_us = 0;       // T_TxAckRcvd
  // Receiver timings and ChunkStatus piggybacked on the ACK.
  uint32_t rx_acquire_us = 0;
  uint32_t rx_payload_us = 0;
  uint8_t rx_chunk_status = 0;
};

// Stage timestamps of a request within a traced Post().
struct RequestTimeline final {
  uint32_t reqid = 0;
  Op op = Op::kWrite;
  size_t len = 0;
  uint64_t t_dequeued_us = 0;         // T_EngineDequeued
  std::vector<ChunkTimeline> chunks;  // Sorted by index.
};

// Stage timestamps of a traced Post().
struct PostTimeline final {
  Handle handle;
  uint64_t t_entry_us = 0;           // T_PostEntry
  uint64_t t_enqueued_us = 0;        // T_EngineEnqueued
  uint64_t t_conn_start_us = 0;      // T_ConnSetupStart, on cold start only
  uint64_t t_host_info_done_us = 0;  // T_GetPeerHostInfoDone, over TCP only
  uint64_t t_conn_done_us = 0;       // T_ConnSetupDone, on cold start only
  uint32_t num_channels = 0;         // Channels connected by T_ConnSetupDone
  uint64_t t_all_done_us = 0;        // T_AllChunksDone
  int16_t all_done_worker_id = 0;    // Worker that recorded T_AllChunksDone
  uint64_t t_completed_us = 0;       // T_CallbackDone or T_PollReaped
  CompletionMode completion_mode = CompletionMode::kUnknown;
  std::vector<RequestTimeline> requests;  // Sorted by T_EngineDequeued.
};

// Returns end_us - start_us, or 0 if either stage was not captured or they are
// out of order.
uint64_t Elapsed(uint64_t start_us, uint64_t end_us);

// Returns when Engine::main_thread_ started processing the Post():
// T_ConnSetupStart on cold start, or the first T_EngineDequeued otherwise.
uint64_t EngineStartUs(const PostTimeline& t);

// Cold-path correlator that groups raw TraceEvent records from TraceBuffer
// by (Handle, ReqId, chunk_t) into per-Post() timelines.
class TraceAssembler final {
 public:
  TraceAssembler() = delete;

  // Returns a PostTimeline for each Post() whose T_PostEntry is in events,
  // sorted by T_PostEntry. Keeps the earliest T_TxAckRcvd and T_AllChunksDone,
  // which repeat if a duplicate chunk is ACKed again.
  static std::vector<PostTimeline> AssembleTimelines(
      absl::Span<const TraceEvent> events);

  // Returns AssembleTimelines(events) summarized into stage durations.
  static std::vector<PostTraceRecord> Assemble(
      absl::Span<const TraceEvent> events);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_ASSEMBLER_H_
