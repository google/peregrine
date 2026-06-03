#include "src/internal/chunk/chunk_test_util.h"

#include <cstddef>
#include <cstdint>

#include "absl/random/random.h"
#include "src/api/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {

void GenChunkMetadata(ChunkMetadata& chunk, const chunk_t index) {
  chunk.base_addr = kBufferBaseAddr;
  chunk.handle = kHandle;
  chunk.buffer = kBuffer;
  chunk.size = kChunkSize;
  chunk.nchunks = kNumChunks;
  chunk.index = index;
}

ChunkMetadata GenChunkMetadata(const chunk_t index) {
  ChunkMetadata chunk;
  GenChunkMetadata(chunk, index);
  return chunk;
}

void GenChunkMetadata(absl::BitGen& bitgen, ChunkMetadata& chunk) {
  chunk.base_addr = addr_t(util::Random<addr_t::ValueType>(bitgen));
  chunk.handle = Handle(util::Random<Handle::ValueType>(bitgen));
  chunk.buffer = Buffer(util::Random<Buffer::ValueType>(bitgen));
  chunk.size = util::Random<uint32_t>(bitgen, 1, 0xffff'ffff);
  chunk.nchunks = util::Random<uint32_t>(bitgen, 1, 0xffff'ffff);
  chunk.index = chunk_t(util::Random<uint32_t>(bitgen, 0, chunk.nchunks - 1));
}

ChunkMetadata GenChunkMetadata(absl::BitGen& bitgen) {
  ChunkMetadata chunk;
  GenChunkMetadata(bitgen, chunk);
  return chunk;
}

ChunkPayloadView GenPayload(const size_t size) {
  auto* addr = reinterpret_cast<const Byte*>(kPayloadSrcAddr.value());
  return ChunkPayloadView(addr, size);
}

}  // namespace peregrine::internal::testing
