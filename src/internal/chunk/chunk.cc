#include "src/internal/chunk/chunk.h"

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal {

std::string ChunkMetadata::ToString() const {
  return absl::StrFormat(
      "ChunkMetadata: handle=0x%x, buffer=0x%x, chunk_size=%d, "
      "#chunks=%d, index=%d",
      handle.value(), buffer.value(), size, nchunks, index.value());
}

}  // namespace peregrine::internal
