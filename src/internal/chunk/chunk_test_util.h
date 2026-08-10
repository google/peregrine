#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>

#include "absl/random/bit_gen_ref.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal::testing {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr addr_t kPayloadSrcAddr(0x12340000);  // read from
constexpr addr_t kBufferBaseAddr(0xffff0000);  // write to
static_assert(kBufferBaseAddr != kPayloadSrcAddr);
constexpr Handle kHandle(0x1234);
constexpr ReqId kReqId(0xbeef);
constexpr uint32_t kChunkSize = 1024;
constexpr uint32_t kNumChunks = 10;
constexpr chunk_t kChunkIndex(1);

// Generates chunk header.
void GenChunkHeader(ChunkHeader& chunk, chunk_t index = kChunkIndex);

// Generates chunk header.
ChunkHeader GenChunkHeader(chunk_t index = kChunkIndex);

// Generates random chunk header.
void GenChunkHeader(absl::BitGenRef bitgen, ChunkHeader& chunk);

// Generates random chunk header.
ChunkHeader GenChunkHeader(absl::BitGenRef bitgen);

// Generates chunk payload view.
ChunkPayloadView GenPayload(size_t size);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
