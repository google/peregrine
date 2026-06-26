#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_

#include <cstddef>
#include <cstdint>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/buffer/buffer_tracker.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/tracker.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements chunk transfer between two endpoints. The chunks are
// arbitrary: they can come from any buffer.
// This class is thread-safe.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

 public:
  // Constructor.
  Transfer(BufferTracker& send, BufferTracker& recv)
      : send_(send), recv_(recv) {}

  // Disable copy and move.
  DISALLOW_COPY(Transfer);
  DISALLOW_MOVE(Transfer);

  // Destructor.
  ~Transfer() = default;

  // Sends chunk metadata and payload to the channel.
  bool SendChunk(Channel* channel, const ChunkMetadata& chunk,
                 ChunkPayloadView payload);

  // Receives chunk metadata and payload from the channel.
  bool RecvChunk(Channel* channel);

  // Returns true iff all the buffers of the `handle` have been sent.
  bool IsSendDone(Handle handle) const { return send_.IsDone(handle); }

  // Returns true iff all the buffers of the `handle` have been received.
  bool IsRecvDone(Handle handle) const { return recv_.IsDone(handle); }

 private:
  // Deserializes chunk metadata and returns true iff the chunk is valid.
  static bool deserialize(Byte* buf, ChunkMetadata& chunk);

  // Returns the send or recv tracker for the given chunk.
  Tracker* getTracker(const ChunkMetadata& chunk) const;

  // Sends an ack chunk with no payload to the channel.
  bool sendAck(Channel* channel, ChunkMetadata& chunk);

 private:
  // Receives chunk metadata and payload from the stream channel.
  bool recvChunkStream(Channel* channel);

  // Receives chunk metadata and payload from the message channel.
  bool recvChunkMsg(Channel* channel);

  // Discards chunk payload that is still buffered in the stream channel.
  static bool drainStream(Channel* channel, uint32_t chunk_size);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10U << 10;

 private:
  BufferTracker& send_;
  BufferTracker& recv_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
