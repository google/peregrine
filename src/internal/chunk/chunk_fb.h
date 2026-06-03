#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FB_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FB_H_

#include <string>
#include <string_view>

#include "src/internal/assumptions.h"
#include "src/internal/chunk/chunk.fbs.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

static_assert(assumptions::kChunkMetadataSerializesToFixedSizeFlatBufString);

// Note: change of this value will cause breaks!
constexpr int kChunkMetadataSerializationSize = 32;
static_assert(kChunkMetadataSerializationSize ==
              sizeof(flatbuf::ChunkMetadata));

// Serializes the chunk metadata to a fixed-size flatbuffer string.
std::string Serialize(const ChunkMetadata& m);

// Parses the chunk metadata from its fixed-size flatbuffer serialization.
ChunkMetadata Deserialize(std::string_view s);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_FB_H_
