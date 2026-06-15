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
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/tracker.h"

namespace peregrine::internal {

using flatbuf::kChunkHeaderSize;

bool Transfer::SendChunk(Channel* const channel, const ChunkMetadata& chunk,
                         const ChunkPayloadView payload) {
  // Step 1: build two iovecs: header + payload.
  const std::string header = flatbuf::Serialize(chunk);
  DCHECK_EQ(header.size(), kChunkHeaderSize);
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };
  DCHECK(IsMatch(chunk, payload));

  // Step 2: send chunk header and payload.
  return channel->Write(iovecs);
}

bool Transfer::RecvChunk(Channel* const channel, ChunkTrackerLookup lookup) {
  const ChannelType t = channel->Type();
  if ABSL_PREDICT_TRUE (IsReliableStream(t)) {
    return recvChunkStream(channel, std::move(lookup));
  } else {
    DCHECK(IsUnreliableMessage(t));
    return recvChunkMsg(channel, std::move(lookup));
  }
}

bool Transfer::deserialize(Byte* buf, ChunkMetadata& chunk) {
  std::string_view s(reinterpret_cast<const char*>(buf), kChunkHeaderSize);
  flatbuf::Deserialize(s, chunk);
  return chunk.IsValid();
}

bool Transfer::recvChunkStream(Channel* const channel,
                               ChunkTrackerLookup lookup) {
  DCHECK(IsReliableStream(channel->Type()));
  DCHECK_NE(lookup, nullptr);

  // Step 1: read chunk header.
  Byte buf[kChunkHeaderSize];
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
  Tracker* const tracker = lookup(chunk.handle, chunk.buffer);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker for: " << chunk.buffer;
    return drainStream(channel, chunk.size);
  }

  // Step 4: acquire chunk write permission and process chunk payload.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  const bool permission = tracker->Acquire(index);
  if ABSL_PREDICT_TRUE (permission) {
    // Permission is granted, read payload and track its arrival.
    const size_t size = chunk.size;
    const bool status = channel->Read(chunk.DstAddr(), size) == size;
    tracker->Release(index, status);
    return status;
  } else {
    // The same chunk is being, or has been, written.
    LOG(WARNING) << "busy/done chunk #" << index.value();
    return drainStream(channel, chunk.size);
  }
}

bool Transfer::recvChunkMsg(Channel* const channel, ChunkTrackerLookup lookup) {
  DCHECK(IsUnreliableMessage(channel->Type()));
  DCHECK_NE(lookup, nullptr);

  // Step 1: read chunk header.
  Byte buf[kTmpBufSize];
  static_assert(kChunkHeaderSize < kTmpBufSize);
  const ssize_t len = channel->Read(buf, kTmpBufSize);
  if ABSL_PREDICT_FALSE (std::cmp_less(len, kChunkHeaderSize)) {
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
  const ChunkPayloadView payload(buf + kChunkHeaderSize,
                                 len - kChunkHeaderSize);
  if ABSL_PREDICT_FALSE (!IsMatch(chunk, payload)) {
    LOG(WARNING) << "mismatched chunk metadata: " << chunk
                 << " vs payload size " << payload.size();
    return false;
  }

  // Step 4: find chunk tracker.
  Tracker* const tracker = lookup(chunk.handle, chunk.buffer);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.buffer.value();
    return false;
  }

  // Step 5: acquire chunk write permission and process chunk payload.
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
