#include "peregrine/src/internal/chunk/chunk.h"

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal {

std::string ChunkHeader::ToString() const {
  return absl::StrFormat(
      "ChunkHeader: handle=0x%x, reqid=0x%x, #chunks=%d, "
      "chunk_index=%d, chunk_addr=0x%llx, chunk_size=%d, "
      "trace={tx_send_mono_us=%llu, rx_payload_us=%u, rx_acquire_us=%u, "
      "rx_worker_id=%d, rx_chunk_status=%u}",
      handle.value(), reqid.value(), nchunks, index.value(), addr.value(), size,
      trace.tx_send_mono_us, trace.rx_payload_us, trace.rx_acquire_us,
      trace.rx_worker_id, trace.rx_chunk_status);
}

}  // namespace peregrine::internal
