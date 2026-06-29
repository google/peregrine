#include "src/internal/chunk/chunk.h"

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal {

std::string ChunkMetadata::ToString() const {
  return absl::StrFormat(
      "ChunkMetadata: handle=0x%x, reqid=0x%x, #chunks=%d, "
      "chunk_index=%d, chunk_addr=0x%llx, chunk_size=%d",
      handle.value(), reqid.value(), nchunks, index.value(), addr.value(),
      size);
}

}  // namespace peregrine::internal
