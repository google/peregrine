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
#include "src/internal/buffer/buffer_tracker.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/tracker.h"

namespace peregrine::internal {

bool Transfer::SendChunk(Channel* const channel, const ChunkMetadata& chunk,
                         const ChunkPayloadView payload) {
  // Step 1: build two iovecs: header + payload.
  const std::string header = ChunkHeader::Serialize(chunk);
  DCHECK_EQ(header.size(), ChunkHeader::kSize);
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };
  DCHECK(IsMatch(chunk, payload));

  // Step 2: send chunk header and payload.
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

bool Transfer::deserialize(Byte* buf, ChunkMetadata& chunk) {
  std::string_view s(reinterpret_cast<const char*>(buf), ChunkHeader::kSize);
  return ChunkHeader::Deserialize(s, chunk) && chunk.IsValid();
}

Tracker* Transfer::getTracker(const ChunkMetadata& chunk) const {
  BufferTracker& t = chunk.IsAck() ? send_ : recv_;
  return t.FindOrCreate(chunk.handle, chunk.buffer, chunk.nchunks);
}

bool Transfer::sendAck(Channel* channel, ChunkMetadata& chunk) {
  chunk.size = 0;
  DCHECK(chunk.IsAck());
  const std::string header = ChunkHeader::Serialize(chunk);
  DCHECK_EQ(header.size(), ChunkHeader::kSize);
  const IoVec iov((void*)header.data(), header.size());
  return channel->Write({iov});
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

  // Step 2: deserialize chunk metadata.
  ChunkMetadata chunk;
  if ABSL_PREDICT_FALSE (!deserialize(buf, chunk)) {
    LOG(WARNING) << "invalid chunk header: " << chunk;
    return false;
  }

  // Step 3: find chunk tracker.
  Tracker* const tracker = getTracker(chunk);
  DCHECK_NE(tracker, nullptr);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.buffer.value();
    return drainStream(channel, chunk.size);
  }

  // Step 3: process ack chunk.
  if (chunk.IsAck()) {
    tracker->Set(chunk.index);
    return true;
  }

  // Step 4: acquire chunk write permission and process chunk payload.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  bool success = false;
  const bool permission = tracker->Acquire(index);
  if ABSL_PREDICT_TRUE (permission) {
    // Permission is granted, read payload and track its arrival.
    const size_t size = chunk.size;
    success = (channel->Read(chunk.DstAddr(), size) == size);
    tracker->Release(index, success);
  } else {
    // The same chunk is being, or has been, written.
    LOG(WARNING) << "busy/done chunk #" << index.value();
    return drainStream(channel, chunk.size);
  }

  // Step 5: send back an ack.
  if ABSL_PREDICT_TRUE (success) {
    if (!sendAck(channel, chunk)) {
      LOG(WARNING) << "failed to send ack";
      return false;
    }
  }
  return success;
}

bool Transfer::recvChunkMsg(Channel* const channel) {
  DCHECK(IsUnreliableMessage(channel->Type()));

  // Step 1: read chunk header.
  Byte buf[kTmpBufSize];
  static_assert(ChunkHeader::kSize < kTmpBufSize);
  const ssize_t len = channel->Read(buf, kTmpBufSize);
  if ABSL_PREDICT_FALSE (std::cmp_less(len, ChunkHeader::kSize)) {
    // TODO(yongx): handle the error.
    LOG(WARNING) << "failed to read chunk header: " << len;
    return false;
  }

  // Step 2: deserialize chunk metadata.
  ChunkMetadata chunk;
  if ABSL_PREDICT_FALSE (!deserialize(buf, chunk)) {
    LOG(WARNING) << "invalid chunk header: " << chunk;
    return false;
  }

  // Step 3: read chunk payload.
  const ChunkPayloadView payload(buf + ChunkHeader::kSize,
                                 len - ChunkHeader::kSize);
  if ABSL_PREDICT_FALSE (!IsMatch(chunk, payload)) {
    LOG(WARNING) << "mismatched chunk metadata: " << chunk
                 << " vs payload size " << payload.size();
    return false;
  }

  // Step 4: find chunk tracker.
  Tracker* const tracker = getTracker(chunk);
  DCHECK_NE(tracker, nullptr);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.buffer.value();
    return false;
  }

  // Step 5: process ack chunk.
  if (chunk.IsAck()) {
    tracker->Set(chunk.index);
    return true;
  }

  // Step 6: acquire chunk write permission and process chunk payload.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  const bool permission = tracker->Acquire(index);
  if ABSL_PREDICT_TRUE (permission) {
    // Permission is granted, read payload and track its arrival.
    std::memcpy(chunk.DstAddr(), payload.data(), payload.size());
    tracker->Release(index, true);
  } else {
    // The same chunk is being, or has been, written.
    LOG(WARNING) << "busy/done chunk #" << index.value();
    return true;
  }

  // Step 7: send back an ack.
  if (!sendAck(channel, chunk)) {
    LOG(WARNING) << "failed to send ack";
    return false;
  }
  return true;
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
