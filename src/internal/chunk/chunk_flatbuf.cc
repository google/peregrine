#include "src/internal/chunk/chunk_flatbuf.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "flatbuffers/include/flatbuffers/flatbuffer_builder.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/chunk/chunk.fbs.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal::flatbuf {

std::string Serialize(const ChunkMetadata& m) {
  static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

  const ChunkHeader h(m.base_addr.value(), m.handle.value(), m.buffer.value(),
                      m.size, m.nchunks, m.index.value());

  flatbuffers::FlatBufferBuilder builder(kChunkHeaderSize * 2);
  builder.Align(8);
  builder.PushBytes(reinterpret_cast<const uint8_t*>(&h), sizeof(h));

  const uint8_t* buf = builder.GetCurrentBufferPointer();
  const size_t size = builder.GetSize();
  DCHECK_EQ(size, kChunkHeaderSize);
  return std::string(reinterpret_cast<const char*>(buf), size);
}

ChunkMetadata Deserialize(const std::string_view s) {
  ChunkMetadata chunk;
  Deserialize(s, chunk);
  return chunk;
}

void Deserialize(std::string_view s, ChunkMetadata& chunk) {
  static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

  // TODO(yongx): remove it if the input string is already 8-byte aligned.
  ChunkHeader h;
  DCHECK_EQ(s.size(), sizeof(h));
  std::memcpy(&h, s.data(), sizeof(h));

  chunk.base_addr = addr_t(h.base_addr());
  chunk.handle = Handle(h.handle());
  chunk.buffer = Buffer(h.buffer());
  chunk.size = h.size();
  chunk.nchunks = h.nchunks();
  chunk.index = chunk_t(h.index());
}

}  // namespace peregrine::internal::flatbuf
