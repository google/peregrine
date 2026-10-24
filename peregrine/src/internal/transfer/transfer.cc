#include "peregrine/src/internal/transfer/transfer.h"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/request/request_tracker.h"

namespace peregrine::internal {

namespace {
using ChunkStatus = ChunkTracker::ChunkStatus;
}  // namespace

bool Transfer::SendInfo(Channel* const channel, const PostInfoHeader& info,
                        absl::Span<const RequestInfo> reqs) {
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  DCHECK(info.IsValid());
  DCHECK(!reqs.empty());

  // Step 1: build post info header and payload.
  const std::string header = HeaderUtil::Serialize(info);
  const std::string payload = HeaderUtil::Serialize(reqs);
  DCHECK(IsMatch(info,
                 ChunkPayloadView(reinterpret_cast<const Byte*>(payload.data()),
                                  payload.size())));
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };

  // Step 2: send them out.
  const ssize_t len = channel->Write(iovecs);
  return std::cmp_equal(len, header.size() + payload.size());
}

bool Transfer::SendChunk(Channel* const channel, const DataHeader& chunk,
                         const ChunkPayloadView payload) {
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  DCHECK(IsMatch(chunk, payload));

  // Step 1: build data header and payload.
  const std::string header = HeaderUtil::Serialize(chunk);
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)header.data(), header.size()),
      IoVec((void*)payload.data(), payload.size()),
  };

  // Step 2: send them out.
  const ssize_t len = channel->Write(iovecs);
  return std::cmp_equal(len, header.size() + payload.size());
}

bool Transfer::Recv(Channel* const channel, RequestTracker& outgoing,
                    RequestTracker& incoming) {
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  const ChannelType t = channel->Type();
  if ABSL_PREDICT_TRUE (t.IsStreamChannel()) {
    return recvStream(channel, outgoing, incoming);
  } else {
    DCHECK(t.IsMessageChannel());
    return recvMsg(channel, outgoing, incoming);
  }
}

bool Transfer::deserialize(const Byte* buf, Header& header) {
  std::string_view s(reinterpret_cast<const char*>(buf), HeaderUtil::kSize);
  if (DataHeader chunk; HeaderUtil::Deserialize(s, chunk)) {
    header = chunk;
    return true;
  }
  if (PostInfoHeader info; HeaderUtil::Deserialize(s, info)) {
    header = info;
    return true;
  }
  return false;
}

bool Transfer::sendAck(Channel* channel, DataHeader& chunk) {
  chunk.len = 0;
  DCHECK(chunk.IsAck());
  const std::string header = HeaderUtil::Serialize(chunk);
  const IoVec iov[] = {{(void*)header.data(), header.size()}};
  const ssize_t len = channel->Write(iov);
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, header.size())) {
    LOG(WARNING) << "failed to send ack: " << chunk;
    return false;
  }
  return true;
}

bool Transfer::recvStream(Channel* const channel, RequestTracker& outgoing,
                          RequestTracker& incoming) {
  DCHECK(channel->Type().IsStreamChannel());
  DCHECK(channel->Type().IsLosslessChannel());

  // Step 1: read 64-byte header.
  Byte buf[HeaderUtil::kSize];
  IoVec iov[] = {{buf, sizeof(buf)}};
  const ssize_t len = channel->Read(iov);
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, sizeof(buf))) {
    LOG(WARNING) << "failed to read header: " << len;
    return false;
  }

  // Step 2: deserialize header and dispatch by active type.
  Header header;
  if ABSL_PREDICT_FALSE (!deserialize(buf, header)) {
    LOG(WARNING) << "invalid header";
    return false;
  }
  if (auto* chunk = std::get_if<DataHeader>(&header)) {
    return recvDataStream(channel, *chunk, outgoing, incoming);
  }
  return recvPostInfoStream(channel, std::get<PostInfoHeader>(header),
                            incoming);
}

bool Transfer::recvPostInfoStream(Channel* const channel,
                                  const PostInfoHeader& info,
                                  RequestTracker& incoming) {
  std::string payload(info.len, '\0');
  IoVec iov[] = {{payload.data(), info.len}};
  const ssize_t len = channel->Read(iov);
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, info.len)) {
    LOG(WARNING) << "failed to read post info payload: " << len;
    return false;
  }

  std::vector<RequestInfo> reqs;
  if ABSL_PREDICT_FALSE (!HeaderUtil::Deserialize(payload, reqs)) {
    LOG(WARNING) << "invalid post info payload";
    return false;
  }
  return incoming.Add(info.handle, reqs, info.metadata);
}

bool Transfer::recvDataStream(Channel* const channel, DataHeader& chunk,
                              RequestTracker& outgoing,
                              RequestTracker& incoming) {
  // Step 1: process ack chunk.
  if (chunk.IsAck()) {
    static_assert(assumptions::kUseAckChunkToSignalChunkWriteCompletion);
    outgoing.Update(chunk.handle, chunk.reqid, chunk.index);
    return true;
  }

  // Step 2: find chunk tracker for incoming data chunk.
  // TODO: Relax the assumption that `PostInfoHeader` has already been processed
  // and registered in `incoming` before `DataHeader` chunks arrive. Also have
  // `incoming.Find()` return `ChunkTracker*` to avoid the extra `Check()` lock
  // + `isComplete()` scan and `.at()` abort on missing `reqid`.
  const size_t size = chunk.len;
  if ABSL_PREDICT_FALSE (incoming.Check(chunk.handle) == Status::kNotFound) {
    LOG(WARNING) << "unregistered handle for data chunk: " << chunk;
    (void)drainStream(channel, size);
    return false;
  }
  ChunkTracker& tracker = incoming.Find(chunk.handle, chunk.reqid);

  // Step 3: process data chunk.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  static_assert(assumptions::kUseAckChunkToSignalChunkWriteCompletion);
  const chunk_t index = chunk.index;
  const ChunkStatus s = tracker.Acquire(index);
  if ABSL_PREDICT_TRUE (s == ChunkStatus::kEmpty) {
    IoVec payload[] = {{chunk.DstAddr(), size}};
    const ssize_t len = channel->Read(payload);
    const bool success = std::cmp_equal(len, size);
    if (tracker.Release(index, success)) {
      incoming.OnReqComplete(chunk.handle, chunk.reqid);
    }
    return success && sendAck(channel, chunk);
  } else {
    DCHECK(s == ChunkStatus::kDone || s == ChunkStatus::kBusy);
    const bool done = s == ChunkStatus::kDone;
    LOG(WARNING) << (done ? "done" : "busy") << " chunk #" << index.value();
    const bool drained = drainStream(channel, size);
    const bool success = !done || sendAck(channel, chunk);
    if (!drained) {
      return false;
    }
    return success;
  }
}

bool Transfer::recvMsg(Channel* const channel, RequestTracker& outgoing,
                       RequestTracker& incoming) {
  DCHECK(channel->Type().IsMessageChannel());

  // Step 1: read packet.
  // TODO: `kTmpBufSize` (10 KiB) bounds the max number of `RequestInfo` entries
  // in a single message-channel `PostInfo` packet; revisit if UDP/message
  // channels need larger `PostInfo` payloads.
  Byte buf[kTmpBufSize];
  static_assert(HeaderUtil::kSize < kTmpBufSize);
  IoVec iov[] = {{buf, kTmpBufSize}};
  const ssize_t len = channel->Read(iov);
  if ABSL_PREDICT_FALSE (std::cmp_less(len, HeaderUtil::kSize)) {
    LOG(WARNING) << "failed to read header: " << len;
    return false;
  }

  // Step 2: deserialize header and dispatch by active type.
  Header header;
  if ABSL_PREDICT_FALSE (!deserialize(buf, header)) {
    LOG(WARNING) << "invalid header";
    return false;
  }
  const ChunkPayloadView payload(buf + HeaderUtil::kSize,
                                 len - HeaderUtil::kSize);
  if (auto* chunk = std::get_if<DataHeader>(&header)) {
    return recvDataMsg(channel, *chunk, payload, outgoing, incoming);
  }
  return recvPostInfoMsg(std::get<PostInfoHeader>(header), payload, incoming);
}

bool Transfer::recvPostInfoMsg(const PostInfoHeader& info,
                               const ChunkPayloadView payload,
                               RequestTracker& incoming) {
  if ABSL_PREDICT_FALSE (!IsMatch(info, payload)) {
    LOG(WARNING) << "mismatched post info header: " << info
                 << " vs payload size: " << payload.size();
    return false;
  }

  std::string_view s(reinterpret_cast<const char*>(payload.data()),
                     payload.size());
  std::vector<RequestInfo> reqs;
  if ABSL_PREDICT_FALSE (!HeaderUtil::Deserialize(s, reqs)) {
    LOG(WARNING) << "invalid post info payload";
    return false;
  }
  return incoming.Add(info.handle, reqs, info.metadata);
}

bool Transfer::recvDataMsg(Channel* const channel, DataHeader& chunk,
                           const ChunkPayloadView payload,
                           RequestTracker& outgoing, RequestTracker& incoming) {
  if ABSL_PREDICT_FALSE (!IsMatch(chunk, payload)) {
    LOG(WARNING) << "mismatched data header: " << chunk
                 << " vs payload size: " << payload.size();
    return false;
  }

  // Step 1: process ack chunk.
  if (chunk.IsAck()) {
    static_assert(assumptions::kUseAckChunkToSignalChunkWriteCompletion);
    outgoing.Update(chunk.handle, chunk.reqid, chunk.index);
    return true;
  }

  // Step 2: find chunk tracker for incoming data chunk.
  // TODO: Relax the assumption that `PostInfoHeader` has already been processed
  // and registered in `incoming` before `DataHeader` chunks arrive. Also have
  // `incoming.Find()` return `ChunkTracker*` to avoid the extra `Check()` lock
  // + `isComplete()` scan and `.at()` abort on missing `reqid`.
  if ABSL_PREDICT_FALSE (incoming.Check(chunk.handle) == Status::kNotFound) {
    LOG(WARNING) << "unregistered handle for data chunk: " << chunk;
    return false;
  }
  ChunkTracker& tracker = incoming.Find(chunk.handle, chunk.reqid);

  // Step 3: process data chunk.
  static_assert(assumptions::kReceiverSideChunkWriteContentionIsVeryLow);
  static_assert(assumptions::kUseAckChunkToSignalChunkWriteCompletion);
  const chunk_t index = chunk.index;
  switch (tracker.Acquire(index)) {
    case ChunkStatus::kEmpty:
      std::memcpy(chunk.DstAddr(), payload.data(), payload.size());
      if (tracker.Release(index, true)) {
        incoming.OnReqComplete(chunk.handle, chunk.reqid);
      }
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
    const size_t size = std::min(left, kTmpBufSize);
    IoVec iov[] = {{buf, size}};
    const ssize_t len = channel->Read(iov);
    if (std::cmp_not_equal(len, size)) return false;
    left -= size;
  }
  return left == 0;
}

}  // namespace peregrine::internal
