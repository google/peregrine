#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/strong_int.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

DEFINE_STRONG_INT_TYPE(addr_t, uintptr_t);
DEFINE_STRONG_INT_TYPE(chunk_t, uint32_t);

// `ChunkHeader` defines chunk metadata.
struct alignas(8) ChunkHeader final {
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  static_assert(assumptions::kChunkHeaderHasBackwardForwardCompatibilityIssue);
  // LINT.IfChange
  Handle handle = Handle(0);   // handle id (fixed)
  ReqId reqid = ReqId(0);      // request id (fixed)
  uint32_t nchunks = 0;        // total #chunks (fixed)
  chunk_t index = chunk_t(0);  // chunk index (variable)
  addr_t addr = addr_t(0);     // chunk address (variable)
  uint32_t size = 0;           // chunk size (variable)
  // LINT.ThenChange(chunk.fbs)

  // Returns true iff the chunk is valid.
  bool IsValid() const {
    DCHECK_LE(0, index.value());
    return 1 <= nchunks && index.value() < nchunks;
  }

  // Returns the destination memory address for the chunk.
  Byte* DstAddr() const { return reinterpret_cast<Byte*>(addr.value()); }

  // Returns true iff the chunk is an ack.
  bool IsAck() const { return size == 0; }

  // Returns a string representation for the chunk.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const ChunkHeader& a, const ChunkHeader& b) {
    return a.handle == b.handle && a.reqid == b.reqid &&
           a.nchunks == b.nchunks && a.index == b.index && a.addr == b.addr &&
           a.size == b.size;
  }
};
static_assert(sizeof(ChunkHeader) == 32);

inline std::ostream& operator<<(std::ostream& os, const ChunkHeader& chunk) {
  return os << chunk.ToString();
}

// `ChunkPayloadView` defines a read-only view to the chunk raw data bytes.
static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
using ChunkPayloadView = absl::Span<const Byte>;
static_assert(sizeof(ChunkPayloadView) == 16);

// Returns true iff the (valid) chunk header matches the payload.
inline bool IsMatch(const ChunkHeader& chunk, ChunkPayloadView payload) {
  DCHECK(chunk.IsValid());
  return chunk.size == payload.size();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_H_
