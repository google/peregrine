#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "src/internal/assumptions.h"
#include "src/internal/chunk/chunk.fbs.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

// This utility class implements (de)serialization for `ChunkMetadata`.
// It is thread-safe since it has no data members.
class ChunkHeader final {
  static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

 public:
  // Note: change of this value will cause breaks!
  static constexpr size_t kSize = 64;
  static_assert(sizeof(flatbuf::ChunkHeader) == kSize);

  // Serializes the chunk metadata to a fixed-size flatbuffer string.
  static std::string Serialize(const ChunkMetadata& m) {
    return serializeV1(m);
  }

  // Parses the chunk metadata from its fixed-size flatbuffer serialization.
  // Returns true iff the parsing is successful.
  static bool Deserialize(std::string_view s, ChunkMetadata& chunk);

 private:
  // Serializes the chunk metadata to a fixed-size flatbuffer string.
  static std::string serializeV1(const ChunkMetadata& m);

  // Parses the chunk metadata from a flatbuffer struct.
  static void deserializeV1(const flatbuf::ChunkHeader& h,
                            ChunkMetadata& chunk);

 private:
  // Serializes the flatbuffer struct to a fixed-size string.
  static std::string serialize(const flatbuf::ChunkHeader& h);

 private:
  // Magic number to identify the chunk serialization format. Do not change!
  static constexpr uint16_t kMagic = (uint16_t)flatbuf::Constant::MAGIC;
  static_assert(kMagic == 0x7067);  // 'p' 'g' in ascii
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FLATBUF_H_
