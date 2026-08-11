#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "src/internal/assumptions.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_generated.h"

namespace peregrine::internal {

// This utility class implements (de)serialization for `ChunkHeader`.
// It is thread-safe since it has no data members.
class ChunkUtil final {
  static_assert(assumptions::kChunkHeaderSerializesTo64BytesFixedSizeFlatBuf);

 public:
  // Note: change of this value will cause breaks!
  static constexpr size_t kSize = 64;
  static_assert(sizeof(flatbuf::ChunkHeader) == kSize);

  // Serializes the chunk header to a fixed-size flatbuffer string.
  static std::string Serialize(const ChunkHeader& chunk) {
    const std::string s = serializeV1(chunk);
    DCHECK_EQ(s.size(), kSize);
    return s;
  }

  // Parses the chunk header from its fixed-size flatbuffer serialization.
  // Returns true iff the parsing is successful.
  static bool Deserialize(std::string_view s, ChunkHeader& chunk);

 private:
  // Serializes the chunk header to a fixed-size flatbuffer string.
  static std::string serializeV1(const ChunkHeader& chunk);

  // Parses the chunk header from a flatbuffer struct.
  static void deserializeV1(const flatbuf::ChunkHeader& h, ChunkHeader& chunk);

 private:
  // Magic number to identify the chunk serialization format. Do not change!
  static constexpr uint16_t kMagic = flatbuf::Constant::Constant_MAGIC;
  static_assert(kMagic == 0x4750);  //  'PG' in little-endian order
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
