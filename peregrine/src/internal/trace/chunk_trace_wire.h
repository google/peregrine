#ifndef PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_

#include <cstdint>

#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine::internal {

// Wire-level per-chunk trace metadata serialized in data chunks.
struct ChunkTraceWire final {
  uint64_t tx_send_mono_us;  // Sender monotonic timestamp in us (0=untraced)
  uint32_t rx_payload_us;    // Receiver payload read/copy duration in us
  uint16_t rx_acquire_us;    // Receiver ChunkTracker::Acquire wait in us
  int8_t rx_worker_id;       // Receiver worker ID (e.g. -1..-N)
  uint8_t rx_chunk_status;   // Receiver ChunkStatus (0=kEmpty,1=kBusy,2=kDone)

  ChunkTraceWire()
      : tx_send_mono_us(0),
        rx_payload_us(0),
        rx_acquire_us(0),
        rx_worker_id(0),
        rx_chunk_status(0) {}

  constexpr bool IsTraced() const { return tx_send_mono_us != 0; }

  // Marks the chunk as traced before enqueuing so worker probes record stages.
  // Worker::SendLoop overwrites tx_send_mono_us with T_WorkerDequeued.
  constexpr void SetTraced() noexcept { tx_send_mono_us = 1; }

  constexpr void SetRx(int worker_id, ChunkTracker::ChunkStatus s,
                       uint64_t t_start_us, uint64_t t_acquired_us,
                       uint64_t t_done_us) noexcept {
    rx_worker_id = ClampI8(worker_id);
    rx_chunk_status = static_cast<uint8_t>(s);
    rx_acquire_us = ClampU16(t_acquired_us - t_start_us);
    rx_payload_us = ClampU32(t_done_us - t_acquired_us);
  }

  friend constexpr bool operator==(const ChunkTraceWire& a,
                                   const ChunkTraceWire& b) = default;
};
static_assert(sizeof(ChunkTraceWire) == 16);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_
