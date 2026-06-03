#include "src/internal/engine/transfer.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal {

namespace {
template <typename T>
T* Ptr(ChunkMetadata& metadata) {
  return reinterpret_cast<T*>(&metadata);
}
}  // namespace

Transfer::Transfer(const ChunkMetadata& m, std::unique_ptr<Channel> channel)
    : template_(m),
      channel_(std::move(channel)),
      tmpbuf_(std::make_unique_for_overwrite<Byte[]>(kTmpBufSize)),
      tracker_(template_.nchunks) {
  DCHECK(template_.IsValid());
  DCHECK_NE(channel_, nullptr);
  DCHECK_NE(tmpbuf_, nullptr);
  DCHECK(tracker_.IsEmpty());
}

bool Transfer::SendChunk(const chunk_t index, const ChunkPayloadView payload) {
  // Step 1: build two iovecs for the chunk
  //  * fix-sized chunk metadata
  //  * variable-sized chunk payload (raw bytes)
  const ChunkMetadata chunk = genChunkMetadata(index);
  const std::array<const IoVec, 2> iovecs = {
      // TODO(yongx): chunk serialization to fixed size header
      IoVec((void*)&chunk, sizeof(chunk)),
      IoVec((void*)payload.data(), payload.size()),
  };
  DCHECK(IsMatch(chunk, payload));

  // Step 2: send the chunk.
  return channel_->Write(iovecs);
}

bool Transfer::RecvChunk() {
  const ChannelType t = channel_->Type();
  if ABSL_PREDICT_TRUE (IsReliableStream(t)) {
    return recvChunkStream();
  } else {
    DCHECK(IsUnreliableMessage(t));
    return testOnly_recvChunkMsg();
  }
}

bool Transfer::recvChunkStream() {
  DCHECK(IsReliableStream(channel_->Type()));

  // Step 1: read chunk metadata.
  ChunkMetadata chunk;
  if (!channel_->ReadExact(Ptr<Byte>(chunk), sizeof(chunk))) {
    // TODO(yongx): handle the error.
    LOG(WARNING) << "failed to read chunk metadata";
    return false;
  }
  if (!chunk.IsValid()) {
    LOG(WARNING) << "invalid chunk metadata: " << chunk;
    return false;
  }
  if (!checkSecurity(chunk)) {
    LOG(WARNING) << "malicious chunk metadata: " << chunk;
    return false;
  }

  // Step 2: acquire chunk write permission and process chunk payload.
  static_assert(assumptions::kLowChunkWritingContentionAtReceiverSide);
  bool status = false;
  const chunk_t index = chunk.index;
  const bool permission = tracker_.Acquire(index);
  if ABSL_PREDICT_TRUE (permission) {
    // Permission is granted, read payload and track its arrival.
    status = channel_->ReadExact(chunk.DstAddr(), chunk.size);
    tracker_.Release(index, status);
  } else {
    // Another thread is busy with the same chunk.
    LOG(WARNING) << "busy chunk #" << index.value();
    status = drainStream(chunk);
  }
  return status;
}

bool Transfer::testOnly_recvChunkMsg() {
  DCHECK(IsUnreliableMessage(channel_->Type()));

  // Step 1: read chunk metadata.
  ChunkMetadata chunk;
  Byte* const buf = tmpbuf_.get();
  const ssize_t len = channel_->ReadUpto(buf, kTmpBufSize);
  if (len < 0) {
    // TODO(yongx): handle the error.
    LOG(WARNING) << "failed to read chunk metadata";
    return false;
  }
  if (len <= sizeof(chunk)) {
    LOG(WARNING) << "too short chunk metadata";
    return false;
  }
  // TODO(yongx): chunk deserialization from fixed size header
  std::memcpy(&chunk, buf, sizeof(chunk));
  if (!chunk.IsValid()) {
    LOG(WARNING) << "invalid chunk metadata: " << chunk;
    return false;
  }
  if (!checkSecurity(chunk)) {
    LOG(WARNING) << "malicious chunk metadata: " << chunk;
    return false;
  }

  // Step 2: read chunk payload.
  const ChunkPayloadView payload(buf + sizeof(chunk), len - sizeof(chunk));
  if (!IsMatch(chunk, payload)) {
    LOG(WARNING) << "mismatched chunk metadata: " << chunk
                 << " vs payload size " << payload.size();
    return false;
  }

  // Step 3: acquire chunk write permission and process chunk payload.
  static_assert(assumptions::kLowChunkWritingContentionAtReceiverSide);
  const chunk_t index = chunk.index;
  const bool permission = tracker_.Acquire(index);
  if ABSL_PREDICT_TRUE (permission) {
    // Permission is granted, read payload and track its arrival.
    std::memcpy(chunk.DstAddr(), payload.data(), payload.size());
    tracker_.Release(index, true);
  } else {
    // Another thread is busy with the same chunk. Discard this payload.
    LOG(WARNING) << "busy chunk #" << index.value();
  }
  return true;
}

bool Transfer::drainStream(const ChunkMetadata& chunk) {
  DCHECK(chunk.IsValid());

  // Drain the chunk payload in the channel.
  LOG(WARNING) << "draining chunk " << chunk;
  size_t left = chunk.size;
  while (left > 0) {
    const size_t len = std::min(left, kTmpBufSize);
    if ABSL_PREDICT_FALSE (!channel_->ReadExact(tmpbuf_.get(), len)) {
      // TODO(yongx): handle the error.
      return false;
    }
    left -= len;
  }
  return true;
}

std::string Transfer::ToString() const {
  return absl::StrFormat(
      "Transfer: handle=0x%x, buffer=0x%x, #chunks=%d, chunk_size=%d",
      template_.handle.value(), template_.buffer.value(), template_.nchunks,
      template_.size);
}

}  // namespace peregrine::internal
