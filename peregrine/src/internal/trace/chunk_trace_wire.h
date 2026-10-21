#ifndef PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_

#include <cstdint>

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

  friend constexpr bool operator==(const ChunkTraceWire& a,
                                   const ChunkTraceWire& b) = default;
};
static_assert(sizeof(ChunkTraceWire) == 16);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_CHUNK_TRACE_WIRE_H_
