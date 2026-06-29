#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_

#include <cstddef>
#include <cstdint>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/buffer/buffer_tracker.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements chunk transfer between two endpoints.
// The chunks are arbitrary: they can come from any buffer.
// This class is thread-safe.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

 public:
  // Constructor.
  Transfer(BufferTracker& outgoing, BufferTracker& incoming)
      : outgoing_(outgoing), incoming_(incoming) {}

  // Disallows copy/move.
  DISALLOW_COPY(Transfer);
  DISALLOW_MOVE(Transfer);

  // Destructor.
  ~Transfer() = default;

  // Sends chunk metadata and payload to the channel.
  bool SendChunk(Channel* channel, const ChunkMetadata& chunk,
                 ChunkPayloadView payload);

  // Receives chunk metadata and payload from the channel.
  bool RecvChunk(Channel* channel);

  // Returns true iff all the buffers of the `handle` have been delivered,
  // i.e, the buffers sent by this endpoint have been received by the peer.
  bool IsSendDone(Handle handle) const {
    return outgoing_.Check(handle) == Status::kSuccess;
  }

  // Returns true iff all the buffers of the `handle` sent by the peer have
  // been received by this endpoint.
  bool IsRecvDone(Handle handle) const {
    return incoming_.Check(handle) == Status::kSuccess;
  }

 private:
  // Deserializes chunk metadata and returns true iff the chunk is valid.
  static bool deserialize(Byte* header, ChunkMetadata& chunk);

  // Returns the outgoing or incoming chunk tracker for the given chunk.
  ChunkTracker* getChunkTracker(const ChunkMetadata& chunk) const;

  // Sends an ack chunk with no payload to the channel.
  bool sendAck(Channel* channel, ChunkMetadata& chunk);

 private:
  // Receives chunk metadata and payload from the reliable stream channel.
  bool recvChunkStream(Channel* channel);

  // Receives chunk metadata and payload from the unreliable message channel.
  bool recvChunkMsg(Channel* channel);

  // Discards chunk payload still buffered in the reliable stream channel.
  static bool drainStream(Channel* channel, uint32_t chunk_size);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10U << 10;
  static_assert(ChunkHeader::kSize < kTmpBufSize);

 private:
  BufferTracker& outgoing_;
  BufferTracker& incoming_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
