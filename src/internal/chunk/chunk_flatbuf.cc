#include "src/internal/chunk/chunk_flatbuf.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "flatbuffers/include/flatbuffers/flatbuffer_builder.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.fbs.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

// Serializes the chunk metadata to a fixed-size flatbuffer string.
std::string ChunkHeader::Serialize(const ChunkMetadata& m) {
  return serializeV1(m);
}

// Parses the chunk metadata from its fixed-size flatbuffer serialization.
// Returns true iff the parsing is successful.
bool ChunkHeader::Deserialize(std::string_view s, ChunkMetadata& chunk) {
  flatbuf::ChunkHeader h;
  DCHECK_EQ(sizeof(h), kSize);
  if (s.size() != sizeof(h)) {
    return false;
  }
  std::memcpy(&h, s.data(), sizeof(h));

  // NOTE: Do not remove any existing case. Only append new cases.
  const uint8_t ver = h.ver();
  switch (ver) {
    case 1:
      deserializeV1(h, chunk);
      return true;
    default:
      LOG(WARNING) << "Unsupported chunk header flatbuf version: " << ver;
      return false;
  }
}

std::string ChunkHeader::serializeV1(const ChunkMetadata& m) {
  const flatbuf::ChunkHeader h(/*ver=*/1, m.handle.value(), m.buffer.value(),
                               m.nchunks, m.index.value(), m.size,
                               m.addr.value(), /*paddings=*/0, 0, 0, 0);

  flatbuffers::FlatBufferBuilder builder(kSize * 2);
  builder.Align(8);
  builder.PushBytes(reinterpret_cast<const uint8_t*>(&h), sizeof(h));

  const uint8_t* buf = builder.GetCurrentBufferPointer();
  const size_t size = builder.GetSize();
  DCHECK_EQ(size, kSize);
  return std::string(reinterpret_cast<const char*>(buf), size);
}

void ChunkHeader::deserializeV1(const flatbuf::ChunkHeader& h,
                                ChunkMetadata& chunk) {
  DCHECK_EQ(h.ver(), 1);
  chunk.handle = Handle(h.handle());
  chunk.buffer = Buffer(h.buffer());
  chunk.nchunks = h.nchunks();
  chunk.index = chunk_t(h.index());
  chunk.addr = addr_t(h.addr());
  chunk.size = h.size();
}

}  // namespace peregrine::internal
