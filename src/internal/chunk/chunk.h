#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "third_party/gloop/util/intops/strong_int.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

DEFINE_STRONG_INT_TYPE(addr_t, uintptr_t);
DEFINE_STRONG_INT_TYPE(chunk_t, uint32_t);

// `ChunkMetadata` defines the metadata of a chunk.
#pragma pack(push, 1)
struct ChunkMetadata final {
  // LINT.IfChange
  Handle handle;     // handle id (fixed)
  Buffer buffer;     // buffer id (fixed)
  uint32_t nchunks;  // total #chunks (fixed)
  chunk_t index;     // chunk index (variable)
  addr_t addr;       // chunk address (variable)
  uint32_t size;     // chunk size (variable)
  // LINT.ThenChange(src/internal/chunk/chunk.fbs)

  // Returns true iff the chunk is valid.
  bool IsValid() const {
    DCHECK_LE(0, index.value());
    return 1 <= nchunks && index.value() < nchunks && 1 <= size;
  }

  // Returns the destination memory address for the chunk.
  Byte* DstAddr() const { return reinterpret_cast<Byte*>(addr.value()); }

  // Returns a string representation for the chunk.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const ChunkMetadata& a, const ChunkMetadata& b) {
    return a.handle == b.handle && a.buffer == b.buffer &&
           a.nchunks == b.nchunks && a.index == b.index && a.addr == b.addr &&
           a.size == b.size;
  }
};
#pragma pack(pop)
static_assert(sizeof(ChunkMetadata) == 28);

inline std::ostream& operator<<(std::ostream& os, const ChunkMetadata& c) {
  return os << c.ToString();
}

// `ChunkPayloadView` defines a read-only view to the chunk raw data bytes.
using ChunkPayloadView = absl::Span<const Byte>;
static_assert(sizeof(ChunkPayloadView) == 16);

// Returns true iff the (valid) chunk metadata matches the payload.
inline bool IsMatch(const ChunkMetadata& chunk, ChunkPayloadView payload) {
  DCHECK(chunk.IsValid());
  return chunk.size == payload.size();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
