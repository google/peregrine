#include "peregrine/src/internal/chunk/header.h"

#include <cstdint>
#include <string>

#include "absl/strings/str_format.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine::internal {

std::string HeaderBase::ToString() const {
  return absl::StrFormat("HeaderBase: opcode=%d, handle=0x%x, len=%d",
                         static_cast<uint8_t>(opcode), handle.value(), len);
}

std::string PostInfoHeader::ToString() const {
  return absl::StrFormat("PostInfoHeader: handle=0x%x, len=%d, peer=%v",
                         handle.value(), len, peer);
}

std::string DataHeader::ToString() const {
  return absl::StrFormat(
      "DataHeader: handle=0x%x, reqid=0x%x, index=%d, byte_offset=%llu, "
      "len=%d, raddr=0x%llx, trace={tx_send_mono_us=%llu, rx_payload_us=%u, "
      "rx_acquire_us=%u, rx_worker_id=%d, rx_chunk_status=%u}",
      handle.value(), reqid.value(), index.value(), byte_offset, len,
      raddr.value(), trace.tx_send_mono_us, trace.rx_payload_us,
      trace.rx_acquire_us, trace.rx_worker_id, trace.rx_chunk_status);
}

std::string RequestInfo::ToString() const {
  return absl::StrFormat(
      "RequestInfo: reqid=0x%x, nchunks=%d, raddr=0x%llx, total_len=%llu, "
      "op=%s, recipe_type=%d",
      reqid.value(), nchunks, raddr.value(), total_len,
      ::peregrine::ToString(op), static_cast<uint8_t>(recipe.type));
}

}  // namespace peregrine::internal
