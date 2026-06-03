#include "src/internal/chunk/chunk_fb.h"

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

namespace peregrine::internal {

std::string Serialize(const ChunkMetadata& m) {
  static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

  const flatbuf::ChunkMetadata b(m.base_addr.value(), m.handle.value(),
                                 m.buffer.value(), m.size, m.nchunks,
                                 m.index.value());

  flatbuffers::FlatBufferBuilder builder(64);
  builder.Align(8);
  builder.PushBytes(reinterpret_cast<const uint8_t*>(&b), sizeof(b));

  const uint8_t* buf = builder.GetCurrentBufferPointer();
  const size_t size = builder.GetSize();
  DCHECK_EQ(size, kChunkMetadataSerializationSize);
  return std::string(reinterpret_cast<const char*>(buf), size);
}

ChunkMetadata Deserialize(const std::string_view s) {
  static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

  // Alignment: copy to a local flatbuffer object.
  flatbuf::ChunkMetadata b;
  DCHECK_EQ(s.size(), sizeof(b));
  std::memcpy(&b, s.data(), sizeof(b));

  return ChunkMetadata{
      .base_addr = addr_t(b.base_addr()),
      .handle = Handle(b.handle()),
      .buffer = Buffer(b.buffer()),
      .size = b.size(),
      .nchunks = b.nchunks(),
      .index = chunk_t(b.index()),
  };
}

}  // namespace peregrine::internal
