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

namespace peregrine::internal {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

DEFINE_STRONG_INT_TYPE(addr_t, uintptr_t);
DEFINE_STRONG_INT_TYPE(chunk_t, uint32_t);

// `ChunkMetadata` defines the metadata of a chunk.
#pragma pack(push, 1)
struct ChunkMetadata final {
  addr_t base_addr;  // buffer base address (fixed) TODO(yongx): remove it
  Handle handle;     // handle id (fixed)
  Buffer buffer;     // buffer id (fixed)
  uint32_t size;     // chunk size (fixed)
  uint32_t nchunks;  // total #chunks (fixed)
  chunk_t index;     // chunk index (variable)

  // Returns true iff the chunk is valid.
  bool IsValid() const {
    DCHECK_LE(0, index.value());
    return 1 <= size && 1 <= nchunks && index.value() < nchunks;
  }

  // Returns true iff the fixed parts of the chunk metadata match.
  bool Check(const ChunkMetadata& m) const {
    return m.base_addr == base_addr && m.handle == handle &&
           m.buffer == buffer && m.size == size && m.nchunks == nchunks;
  }

  // Returns the destination memory address for the chunk.
  Byte* DstAddr() const {
    static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
    const addr_t::ValueType i = index.value();
    return reinterpret_cast<Byte*>(base_addr.value() + i * size);
  }

  // Returns a string representation for the chunk.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const ChunkMetadata& a, const ChunkMetadata& b) {
    return a.base_addr == b.base_addr && a.handle == b.handle &&
           a.buffer == b.buffer && a.size == b.size && a.nchunks == b.nchunks &&
           a.index == b.index;
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
