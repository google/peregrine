#include "src/internal/engine/transfer.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/request/request_tracker.h"

namespace peregrine::internal {

bool Transfer::SendChunk(Channel* const channel, const ChunkMetadata& chunk,
                         const ChunkPayloadView payload) {
  DCHECK(IsMatch(chunk, payload));

  // Step 1: build chunk header and payload.
  const std::string header = ChunkHeader::Serialize(chunk);
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };

  // Step 2: send them out.
  return channel->Write(iovecs);
}

bool Transfer::RecvChunk(Channel* const channel) {
  const ChannelType t = channel->Type();
  if ABSL_PREDICT_TRUE (IsReliableStream(t)) {
    return recvChunkStream(channel);
  } else {
    DCHECK(IsUnreliableMessage(t));
    return recvChunkMsg(channel);
  }
}

bool Transfer::deserialize(Byte* header, ChunkMetadata& chunk) {
  std::string_view s(reinterpret_cast<const char*>(header), ChunkHeader::kSize);
  return ChunkHeader::Deserialize(s, chunk) && chunk.IsValid();
}

ChunkTracker* Transfer::getChunkTracker(const ChunkMetadata& chunk) const {
  RequestTracker& t = chunk.IsAck() ? outgoing_ : incoming_;
  return t.FindOrCreate(chunk.handle, chunk.reqid, chunk.nchunks);
}

bool Transfer::sendAck(Channel* channel, ChunkMetadata& chunk) {
  chunk.size = 0;
  DCHECK(chunk.IsAck());
  const std::string header = ChunkHeader::Serialize(chunk);
  const IoVec iov((void*)header.data(), header.size());
  if ABSL_PREDICT_FALSE (!channel->Write({iov})) {
    LOG(WARNING) << "failed to send ack: " << chunk;
    return false;
  }
  return true;
}

bool Transfer::recvChunkStream(Channel* const channel) {
  DCHECK(IsReliableStream(channel->Type()));

  // Step 1: read chunk header.
  Byte buf[ChunkHeader::kSize];
  const ssize_t len = channel->Read(buf, sizeof(buf));
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, sizeof(buf))) {
    // TODO(yongx): handle the error.
    LOG(WARNING) << "failed to read chunk header: " << len;
    return false;
  }

  // Step 2: deserialize chunk header.
  ChunkMetadata chunk;
  if ABSL_PREDICT_FALSE (!deserialize(buf, chunk)) {
    LOG(WARNING) << "invalid chunk header: " << chunk;
    return false;
  }

  // Step 3: find chunk tracker.
  ChunkTracker* const tracker = getChunkTracker(chunk);
  DCHECK_NE(tracker, nullptr);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.reqid.value();
    return drainStream(channel, chunk.size);
  }

  // Step 4: process ack chunk.
  if (chunk.IsAck()) {
    tracker->Set(chunk.index);
    return true;
  }

  // Step 5: process data chunk.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  if ABSL_PREDICT_TRUE (tracker->Acquire(index)) {
    // Write permission granted, read payload and track data arrival.
    const size_t size = chunk.size;
    const bool success = (channel->Read(chunk.DstAddr(), size) == size);
    tracker->Release(index, success);
    return success && sendAck(channel, chunk);
  } else {
    // The same chunk is being, or has been, written.
    LOG(WARNING) << "busy/done chunk #" << index.value();
    return drainStream(channel, chunk.size);
  }
}

bool Transfer::recvChunkMsg(Channel* const channel) {
  DCHECK(IsUnreliableMessage(channel->Type()));

  // Step 1: read chunk header.
  Byte buf[kTmpBufSize];
  static_assert(ChunkHeader::kSize < kTmpBufSize);
  const ssize_t len = channel->Read(buf, kTmpBufSize);
  if ABSL_PREDICT_FALSE (std::cmp_less(len, ChunkHeader::kSize)) {
    LOG(WARNING) << "failed to read chunk header: " << len;
    return false;
  }

  // Step 2: deserialize chunk header.
  ChunkMetadata chunk;
  if ABSL_PREDICT_FALSE (!deserialize(buf, chunk)) {
    LOG(WARNING) << "invalid chunk header: " << chunk;
    return false;
  }

  // Step 3: check chunk payload size.
  const ChunkPayloadView payload(buf + ChunkHeader::kSize,
                                 len - ChunkHeader::kSize);
  if ABSL_PREDICT_FALSE (!IsMatch(chunk, payload)) {
    LOG(WARNING) << "mismatched chunk metadata: " << chunk
                 << " vs payload size: " << payload.size();
    return false;
  }

  // Step 4: find chunk tracker.
  ChunkTracker* const tracker = getChunkTracker(chunk);
  DCHECK_NE(tracker, nullptr);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.reqid.value();
    return false;
  }

  // Step 5: process ack chunk.
  if (chunk.IsAck()) {
    tracker->Set(chunk.index);
    return true;
  }

  // Step 6: process data chunk.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  if ABSL_PREDICT_TRUE (tracker->Acquire(index)) {
    // Write permission granted, read payload and track data arrival.
    std::memcpy(chunk.DstAddr(), payload.data(), payload.size());
    tracker->Release(index, true);
    return sendAck(channel, chunk);
  } else {
    // The same chunk is being, or has been, written.
    LOG(WARNING) << "busy/done chunk #" << index.value();
    return true;
  }
}

bool Transfer::drainStream(Channel* const channel, const uint32_t chunk_size) {
  LOG(WARNING) << "draining chunk size " << chunk_size;
  Byte buf[kTmpBufSize];
  size_t left = chunk_size;
  while (left > 0) {
    const size_t len = std::min(left, kTmpBufSize);
    if ABSL_PREDICT_FALSE (channel->Read(buf, len) != len) {
      // TODO(yongx): handle the error.
      return false;
    }
    left -= len;
  }
  return true;
}

}  // namespace peregrine::internal
