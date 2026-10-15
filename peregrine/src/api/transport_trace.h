#ifndef PEREGRINE_SRC_API_TRANSPORT_TRACE_H_
#define PEREGRINE_SRC_API_TRANSPORT_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "peregrine/src/api/transport_types.h"

namespace peregrine {

// Configuration for transport request tracing.
struct TraceConfig final {
  // Sampling interval for Transport::Post() calls:
  //   0 = tracing disabled (default)
  //   1 = always-on (trace every Post() call)
  //   N > 1 = trace 1 out of every N Post() calls
  uint32_t sample_every_n = 0;
};

// How the application picked up a completed Post().
enum class CompletionMode : uint8_t {
  kUnknown = 0,   // Not completed yet, or the completion was not captured
  kCallback = 1,  // on_complete returned (T_CallbackDone)
  kPoll = 2,      // Transport::Poll() reaped the handle (T_PollReaped)
};

// Per-chunk stage durations of a traced Post(), in microseconds.
struct ChunkTraceSummary final {
  uint32_t chunk_index = 0;
  uint32_t size_bytes = 0;
  // Sender worker ID (+1..+N).
  int16_t send_worker_id = 0;
  // Receiver worker ID (-1..-N) from the ACK, or 0 if no ACK was captured.
  int16_t recv_worker_id = 0;
  // T_WorkerDequeued - T_WorkerEnqueued.
  uint64_t worker_queue_wait_us = 0;
  // T_TxWriteDone - T_WorkerDequeued.
  uint64_t tx_write_us = 0;
  // Receiver ChunkTracker::Acquire wait, piggybacked on the ACK.
  uint64_t rx_acquire_wait_us = 0;
  // Receiver payload read and copy, piggybacked on the ACK.
  uint64_t rx_payload_write_us = 0;
  // T_TxAckRcvd - T_WorkerDequeued.
  uint64_t chunk_rtt_us = 0;
  // Receiver ChunkStatus from the ACK (0 = kEmpty, 1 = kBusy, 2 = kDone).
  // Also 0 if no ACK was captured.
  uint8_t rx_chunk_status = 0;
};

// Per-request stage durations within a traced Post(), in microseconds.
struct RequestTraceSummary final {
  uint32_t reqid = 0;
  Op op = Op::kWrite;
  size_t len = 0;
  // Last T_WorkerEnqueued - T_EngineDequeued. Engine::main_thread_ dequeues
  // all requests of a Post() before enqueuing any chunk, so this includes the
  // chunk fan-out of earlier requests in the same Post().
  uint64_t chunk_fanout_us = 0;
  std::vector<ChunkTraceSummary> chunks;
};

// End-to-end trace record of a single Transport::Post() call. Timestamps are
// monotonic clock readings and durations are in microseconds. A duration is 0
// if either of its stages was not captured.
struct PostTraceRecord final {
  Handle handle;
  // T_PostEntry.
  uint64_t post_start_us = 0;
  // T_EngineEnqueued - T_PostEntry.
  uint64_t enqueue_overhead_us = 0;
  // Wait until Engine::main_thread_ starts processing the Post(), i.e.
  // T_ConnSetupStart on cold start or the first T_EngineDequeued otherwise,
  // minus T_EngineEnqueued.
  uint64_t engine_queue_wait_us = 0;
  // T_ConnSetupDone - T_ConnSetupStart on cold start, or 0 if the peer was
  // already connected.
  uint64_t conn_setup_us = 0;
  // T_AllChunksDone - T_PostEntry.
  uint64_t transport_e2e_us = 0;
  // T_CallbackDone or T_PollReaped (see completion_mode) - T_AllChunksDone.
  uint64_t completion_pickup_us = 0;
  CompletionMode completion_mode = CompletionMode::kUnknown;
  std::vector<RequestTraceSummary> requests;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_TRACE_H_
