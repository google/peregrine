#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "src/internal/assumptions.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_generated.h"

namespace peregrine::internal {

// This utility class implements (de)serialization for `ChunkHeader`.
// It is thread-safe since it has no data members.
class ChunkUtil final {
  static_assert(assumptions::kChunkHeaderSerializesTo64BytesFixedSizeFlatBuf);
  static_assert(assumptions::kChunkHeaderHasBackwardForwardCompatibilityIssue);

 public:
  // Note: change of this value will cause breaks!
  static constexpr size_t kSize = 64;
  static_assert(sizeof(flatbuf::ChunkHeader) == kSize);

  // Serializes the chunk header to a fixed-size flatbuffer string.
  // TODO(yongx): remove the default value for the version parameter.
  static std::string Serialize(const ChunkHeader& chunk, uint16_t ver = 1);

  // Parses the chunk header from its fixed-size flatbuffer serialization.
  // Returns true iff the parsing is successful.
  static bool Deserialize(std::string_view s, ChunkHeader& chunk);

 private:
  // Magic number to identify the chunk serialization format. Do not change!
  static constexpr uint16_t kMagic = flatbuf::Constant::Constant_MAGIC;
  static_assert(kMagic == 0x4750);  //  'PG' in little-endian order

 private:
  // Serializes the chunk header to a fixed-size flatbuffer string.
  static std::string serializeV1(const ChunkHeader& chunk);

  // Parses the chunk header from a flatbuffer struct.
  static void deserializeV1(const flatbuf::ChunkHeader& h, ChunkHeader& chunk);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
