#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>

#include "absl/random/random.h"
#include "src/api/types.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal::testing {

constexpr addr_t kPayloadSrcAddr(0x12340000);  // read from
constexpr addr_t kBufferBaseAddr(0xffff0000);  // write to
static_assert(kBufferBaseAddr != kPayloadSrcAddr);
constexpr Handle kHandle(0x1234);
constexpr Buffer kBuffer(0xbeef);
constexpr uint32_t kChunkSize = 1024;
constexpr uint32_t kNumChunks = 10;
constexpr chunk_t kChunkIndex(1);

// Generates chunk metadata.
void GenChunkMetadata(ChunkMetadata& chunk, chunk_t index = kChunkIndex);

// Generates chunk metadata.
ChunkMetadata GenChunkMetadata(chunk_t index = kChunkIndex);

// Generates random chunk metadata.
void GenChunkMetadata(absl::BitGen& bitgen, ChunkMetadata& chunk);

// Generates random chunk metadata.
ChunkMetadata GenChunkMetadata(absl::BitGen& bitgen);

// Generates chunk payload view.
ChunkPayloadView GenPayload(size_t size);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
