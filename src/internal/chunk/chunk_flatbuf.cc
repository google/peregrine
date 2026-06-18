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

bool ChunkHeader::Deserialize(std::string_view s, ChunkMetadata& chunk) {
  flatbuf::ChunkHeader h;
  DCHECK_EQ(sizeof(h), kSize);
  if (s.size() != sizeof(h)) {
    return false;
  }

  std::memcpy(&h, s.data(), sizeof(h));
  if (h.magic() != kMagic) {
    return false;
  }

  // NOTE: Do not remove any existing case. Only prepend new cases.
  const uint8_t ver = h.ver();
  switch (ver) {
    case 2:
      deserializeV2(h, chunk);
      return true;
    case 1:
      deserializeV1(h, chunk);
      return true;
    default:
      LOG(WARNING) << "Unsupported chunk header flatbuf version: " << ver;
      return false;
  }
}

std::string ChunkHeader::serializeV2(const ChunkMetadata& m) {
  constexpr uint16_t kVer = 2;
  const flatbuf::ChunkHeader h(kMagic, kVer, m.handle.value(), m.buffer.value(),
                               m.nchunks, m.index.value(), m.size,
                               m.addr.value(), /*send_ts=*/1000,
                               /*recv_ts=*/2000, /*paddings=*/0, 0);
  return serialize(h);
}

void ChunkHeader::deserializeV2(const flatbuf::ChunkHeader& h,
                                ChunkMetadata& chunk) {
  DCHECK_EQ(h.ver(), 2);
  chunk.handle = Handle(h.handle());
  chunk.buffer = Buffer(h.buffer());
  chunk.nchunks = h.nchunks();
  chunk.index = chunk_t(h.index());
  chunk.addr = addr_t(h.addr());
  chunk.size = h.size();
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

std::string ChunkHeader::serialize(const flatbuf::ChunkHeader& h) {
  flatbuffers::FlatBufferBuilder builder(sizeof(h) * 2);
  builder.Align(8);
  builder.PushBytes(reinterpret_cast<const uint8_t*>(&h), sizeof(h));

  const uint8_t* buf = builder.GetCurrentBufferPointer();
  const size_t size = builder.GetSize();
  DCHECK_EQ(size, kSize);
  return std::string(reinterpret_cast<const char*>(buf), size);
}

}  // namespace peregrine::internal
