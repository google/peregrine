#ifndef PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_

#include <cstddef>
#include <cstdint>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/request/request_tracker.h"

namespace peregrine::internal {

// This utility class implements chunk transfer over a channel.
// The chunks are arbitrary: they can come from any buffer.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

 public:
  // Sends chunk metadata and payload to the channel.
  static bool SendChunk(Channel* channel, const ChunkMetadata& chunk,
                        ChunkPayloadView payload);

  // Receives chunk metadata and payload from the channel.
  static bool RecvChunk(Channel* channel, RequestTracker& outgoing,
                        RequestTracker& incoming);

 private:
  // Deserializes chunk metadata and returns true iff the chunk is valid.
  static bool deserialize(Byte* header, ChunkMetadata& chunk);

  // Returns the outgoing or incoming chunk tracker for the given chunk.
  static ChunkTracker* getChunkTracker(const ChunkMetadata& chunk,
                                       RequestTracker& outgoing,
                                       RequestTracker& incoming);

  // Sends an ack chunk with no payload to the channel.
  static bool sendAck(Channel* channel, ChunkMetadata& chunk);

 private:
  // Receives chunk metadata and payload from the reliable stream channel.
  static bool recvChunkStream(Channel* channel, RequestTracker& outgoing,
                              RequestTracker& incoming);

  // Receives chunk metadata and payload from the message channel.
  static bool recvChunkMsg(Channel* channel, RequestTracker& outgoing,
                           RequestTracker& incoming);

  // Discards chunk payload still buffered in the reliable stream channel.
  static bool drainStream(Channel* channel, uint32_t chunk_size);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10U << 10;
  static_assert(ChunkHeader::kSize < kTmpBufSize);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_
