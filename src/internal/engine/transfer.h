#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>

#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal {

// This class implements transfer for a single buffer between two endpoints.
// A buffer is a variable-sized contiguous memory space. It can be split into
// multiple equal-sized chunks, except for the last one.
//
// This class is thread-compatible but not thread-safe. One instance of this
// class should be used by a single thread.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

 public:
  // Constructor.
  Transfer(const ChunkMetadata& m, std::unique_ptr<Channel> channel);

  // Sends chunk metadata and payload to the channel.
  bool SendChunk(chunk_t index, ChunkPayloadView payload);

  // Receives chunk metadata and payload from the channel.
  bool RecvChunk();

  // Returns true iff all the chunks of this buffer have been delivered.
  bool Done() const { return tracker_.IsCompleted(); }

  // Returns a string representation for the transfer.
  std::string ToString() const;

 private:
  // Generates chunk metadata with the given chunk `index`.
  ChunkMetadata genChunkMetadata(chunk_t index) {
    ChunkMetadata c = template_;
    c.index = index;
    return c;
  }

  // Returns true iff the incoming `chunk` metadata is not malicious.
  bool checkSecurity(const ChunkMetadata& chunk) const {
    return template_.Check(chunk);
  }

  // Receives chunk metadata and payload from the stream channel.
  bool recvChunkStream();

  // Receives chunk metadata and payload from the message channel.
  bool testOnly_recvChunkMsg();

  // Discards chunk payload that is still buffered in the stream channel.
  bool drainStream(const ChunkMetadata& chunk);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10UL << 10;

  const ChunkMetadata template_;
  std::unique_ptr<Channel> channel_;
  std::unique_ptr<Byte[]> tmpbuf_;
  ChunkTracker tracker_;
};

inline std::ostream& operator<<(std::ostream& os, const Transfer& t) {
  return os << t.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_TRANSFER_H_
