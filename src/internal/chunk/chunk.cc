#include "src/internal/chunk/chunk.h"

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal {

std::string ChunkMetadata::ToString() const {
  return absl::StrFormat(
      "ChunkMetadata: buffer=0x%x, chunk_size=%d, #chunks=%d, chunk_index=%d",
      buffer.value(), chunk_size, nchunks, index.value());
}

}  // namespace peregrine::internal
