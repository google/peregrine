#include "src/internal/transfer/transfer.h"

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
#include "src/internal/channel/channel_types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/internal/request/request_tracker.h"

namespace peregrine::internal {

using ChunkStatus = ChunkTracker::ChunkStatus;

bool Transfer::SendChunk(Channel* const channel, const ChunkMetadata& chunk,
                         const ChunkPayloadView payload) {
  static_assert(assumptions::kChunkMetadataAndPayloadAreEncryptedOnWire);
  DCHECK(IsMatch(chunk, payload));

  // Step 1: build chunk header and payload.
  const std::string header = ChunkHeader::Serialize(chunk);
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };

  // Step 2: send them out.
  return channel->WriteV(iovecs) == header.size() + payload.size();
}

bool Transfer::RecvChunk(Channel* const channel, RequestTracker& outgoing,
                         RequestTracker& incoming) {
  static_assert(assumptions::kChunkMetadataAndPayloadAreEncryptedOnWire);
  const ChannelType t = channel->Type();
  if ABSL_PREDICT_TRUE (IsReliableStream(t)) {
    return recvChunkStream(channel, outgoing, incoming);
  } else {
    DCHECK(IsMessageChannel(t));
    return recvChunkMsg(channel, outgoing, incoming);
  }
}

bool Transfer::deserialize(Byte* header, ChunkMetadata& chunk) {
  std::string_view s(reinterpret_cast<const char*>(header), ChunkHeader::kSize);
  return ChunkHeader::Deserialize(s, chunk) && chunk.IsValid();
}

ChunkTracker* Transfer::getChunkTracker(const ChunkMetadata& chunk,
                                        RequestTracker& outgoing,
                                        RequestTracker& incoming) {
  RequestTracker& t = chunk.IsAck() ? outgoing : incoming;
  return t.FindOrCreate(chunk.handle, chunk.reqid, chunk.nchunks);
}

bool Transfer::sendAck(Channel* channel, ChunkMetadata& chunk) {
  chunk.size = 0;
  DCHECK(chunk.IsAck());
  const std::string header = ChunkHeader::Serialize(chunk);
  const Byte* const buf = reinterpret_cast<const Byte*>(header.data());
  if ABSL_PREDICT_FALSE (channel->Write(buf, header.size()) != header.size()) {
    LOG(WARNING) << "failed to send ack: " << chunk;
    return false;
  }
  return true;
}

bool Transfer::recvChunkStream(Channel* const channel, RequestTracker& outgoing,
                               RequestTracker& incoming) {
  DCHECK(IsReliableStream(channel->Type()));

  // Step 1: read chunk header.
  Byte buf[ChunkHeader::kSize];
  const ssize_t len = channel->Read(buf, sizeof(buf));
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, sizeof(buf))) {
    // TODO(yongx): drop this channel.
    LOG(WARNING) << "failed to read chunk header: " << len;
    return false;
  }

  // Step 2: deserialize chunk header.
  ChunkMetadata chunk;
  if ABSL_PREDICT_FALSE (!deserialize(buf, chunk)) {
    // TODO(yongx): drop this channel.
    LOG(WARNING) << "invalid chunk header";
    return false;
  }

  // Step 3: find chunk tracker.
  ChunkTracker* const tracker = getChunkTracker(chunk, outgoing, incoming);
  DCHECK_NE(tracker, nullptr);
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    LOG(WARNING) << "failed to find chunk tracker: " << chunk.reqid.value();
    if (!drainStream(channel, chunk.size)) {
      // TODO(yongx): drop this channel.
    }
    return false;
  }

  // Step 4: process ack chunk.
  if (chunk.IsAck()) {
    tracker->Set(chunk.index);
    return true;
  }

  // Step 5: process data chunk.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  const chunk_t index = chunk.index;
  const size_t size = chunk.size;
  const ChunkStatus s = tracker->Acquire(index);
  if ABSL_PREDICT_TRUE (s == ChunkStatus::kEmpty) {
    // Write permission granted, read payload and track data arrival.
    const bool success = (channel->Read(chunk.DstAddr(), size) == size);
    tracker->Release(index, success);
    return success && sendAck(channel, chunk);
  } else {
    DCHECK(s == ChunkStatus::kDone || s == ChunkStatus::kBusy);
    const bool done = s == ChunkStatus::kDone;
    LOG(WARNING) << (done ? "done" : "busy") << " chunk #" << index.value();
    const bool drained = drainStream(channel, size);
    const bool success = !done || sendAck(channel, chunk);
    if (!drained) {
      // TODO(yongx): drop this channel.
      return false;
    }
    return success;
  }
}

bool Transfer::recvChunkMsg(Channel* const channel, RequestTracker& outgoing,
                            RequestTracker& incoming) {
  DCHECK(IsMessageChannel(channel->Type()));

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
    LOG(WARNING) << "invalid chunk header";
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
  ChunkTracker* const tracker = getChunkTracker(chunk, outgoing, incoming);
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
  switch (tracker->Acquire(index)) {
    case ChunkStatus::kEmpty:
      std::memcpy(chunk.DstAddr(), payload.data(), payload.size());
      tracker->Release(index, true);
      return sendAck(channel, chunk);
    case ChunkStatus::kDone:
      LOG(WARNING) << "done chunk #" << index.value();
      return sendAck(channel, chunk);
    case ChunkStatus::kBusy:
      LOG(WARNING) << "busy chunk #" << index.value();
      return true;
  }
}

bool Transfer::drainStream(Channel* const channel, const uint32_t chunk_size) {
  LOG(WARNING) << "draining chunk size " << chunk_size;
  Byte buf[kTmpBufSize];
  size_t left = chunk_size;
  while (left > 0) {
    const size_t len = std::min(left, kTmpBufSize);
    if (channel->Read(buf, len) != len) return false;
    left -= len;
  }
  return left == 0;
}

}  // namespace peregrine::internal
