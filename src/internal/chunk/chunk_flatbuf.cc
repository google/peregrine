#include "src/internal/chunk/chunk_flatbuf.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_generated.h"

namespace peregrine::internal {

static_assert(assumptions::kChunkHeaderSerializesTo64BytesFixedSizeFlatBuf);

bool ChunkUtil::Deserialize(std::string_view s, ChunkHeader& chunk) {
  DCHECK_EQ(sizeof(flatbuf::ChunkHeader), kSize);
  if (s.size() != kSize) {
    return false;
  }

  alignas(flatbuf::ChunkHeader) uint8_t buf[kSize];
  DCHECK_EQ(sizeof(buf), s.size());
  std::memcpy(buf, s.data(), kSize);

  const auto& h = *reinterpret_cast<const flatbuf::ChunkHeader*>(buf);
  if (h.magic() != kMagic) {
    return false;
  }

  // NOTE: Do not remove any existing case. Only prepend new ones.
  const uint16_t ver = h.ver();
  switch (ver) {
    case 1:
      deserializeV1(h, chunk);
      return true;
    default:
      LOG(ERROR) << "Unsupported chunk header flatbuf version: " << ver;
      return false;
  }
}

std::string ChunkUtil::serializeV1(const ChunkHeader& chunk) {
  constexpr uint16_t kVer = 1;
  const flatbuf::ChunkHeader h(kMagic, kVer, chunk.handle.value(),
                               chunk.reqid.value(), chunk.nchunks,
                               chunk.index.value(), chunk.size,
                               chunk.addr.value(), /*paddings=*/0, 0, 0, 0);
  DCHECK_EQ(sizeof(h), kSize);
  return std::string(reinterpret_cast<const char*>(&h), sizeof(h));
}

void ChunkUtil::deserializeV1(const flatbuf::ChunkHeader& h,
                              ChunkHeader& chunk) {
  DCHECK_EQ(h.ver(), 1);
  chunk.handle = Handle(h.handle());
  chunk.reqid = ReqId(h.reqid());
  chunk.nchunks = h.nchunks();
  chunk.index = chunk_t(h.index());
  chunk.addr = addr_t(h.addr());
  chunk.size = h.size();
}

}  // namespace peregrine::internal
