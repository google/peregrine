#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal {

// This utility class implements chunk transfer between two endpoints.
// The chunk is arbitrary: it can come from any buffer. A buffer is a
// variable-sized contiguous memory space. It can be split into multiple
// fixed-sized chunks, except for the last one.
//
// This class is thread-compatible but not thread-safe.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
  using ChunkTrackerLookup = absl::AnyInvocable<ChunkTracker*(Handle, Buffer)>;

 public:
  // Constructor.
  Transfer() : tmpbuf_(std::make_unique_for_overwrite<Byte[]>(kTmpBufSize)) {}

  // Sends chunk metadata and payload to the channel.
  static bool SendChunk(Channel* channel, const ChunkMetadata& chunk,
                        ChunkPayloadView payload);

  // Receives chunk metadata and payload from the channel.
  bool RecvChunk(Channel* channel, ChunkTrackerLookup lookup);

 private:
  // Deserializes chunk metadata and returns true iff the chunk is valid.
  static bool deserialize(Byte* buf, ChunkMetadata& chunk);

  // Receives chunk metadata and payload from the stream channel.
  bool recvChunkStream(Channel* channel, ChunkTrackerLookup lookup);

  // Receives chunk metadata and payload from the message channel.
  bool testOnly_recvChunkMsg(Channel* channel, ChunkTrackerLookup lookup);

  // Discards chunk payload that is still buffered in the stream channel.
  bool drainStream(Channel* channel, uint32_t chunk_size);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10U << 10;

  std::unique_ptr<Byte[]> tmpbuf_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
