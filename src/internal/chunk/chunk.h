#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/types.h"
#include "util/intops/strong_int.h"

namespace peregrine::internal {

DEFINE_STRONG_INT_TYPE(addr_t, uintptr_t);
DEFINE_STRONG_INT_TYPE(chunk_t, uint32_t);

// A buffer is a contiguous memory. It can be divided into multiple chunks.
// Chunk is the smallest data transmit unit. It includes two parts:
// `ChunkMetadata` and `ChunkPayloadView`.

// `ChunkMetadata` defines the metadata of a chunk.
struct ChunkMetadata final {
  addr_t base_addr;     // buffer base address (fixed) TODO(yongx): remove it
  Buffer buffer;        // buffer id (fixed)
  uint32_t chunk_size;  // chunk size (fixed)
  uint32_t nchunks;     // total #chunks (fixed)
  chunk_t index;        // chunk index (variable)

  // Returns true iff the chunk is valid.
  bool IsValid() const {
    DCHECK_LE(0, index.value());
    return 1 <= chunk_size && 1 <= nchunks && index.value() < nchunks;
  }

  // Returns the destination memory address for the chunk.
  Byte* DstAddr() const {
    const uintptr_t i = index.value();
    return reinterpret_cast<Byte*>(base_addr.value() + i * chunk_size);
  }

  // Returns a string representation for the chunk.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const ChunkMetadata& a, const ChunkMetadata& b) {
    return a.base_addr == b.base_addr && a.buffer == b.buffer &&
           a.chunk_size == b.chunk_size && a.nchunks == b.nchunks &&
           a.index == b.index;
  }
};
static_assert(sizeof(ChunkMetadata) == 24);

inline std::ostream& operator<<(std::ostream& os, const ChunkMetadata& c) {
  return os << c.ToString();
}

// `ChunkPayloadView` defines a read-only view to the chunk raw data bytes.
using ChunkPayloadView = absl::Span<const Byte>;
static_assert(sizeof(ChunkPayloadView) == 16);

// Returns true iff the (valid) chunk metadata matches the payload.
inline bool IsMatch(const ChunkMetadata& chunk, ChunkPayloadView payload) {
  DCHECK(chunk.IsValid());
  return chunk.chunk_size == payload.size();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
