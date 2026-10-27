#ifndef PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_
#define PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_

#include <cstddef>
#include <cstdint>
#include <variant>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal {

// This utility class implements chunk transfer over a channel.
// The chunks are arbitrary: they can come from any buffer.
class Transfer final {
  static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

 public:
  // Sends `PostInfoHeader` and its `RequestInfo` payload to the channel.
  static bool SendInfo(Channel* channel, const PostInfoHeader& info,
                       absl::Span<const RequestInfo> reqs);

  // Sends `DataHeader` and payload to the channel.
  static bool SendChunk(Channel* channel, const DataHeader& chunk,
                        ChunkPayloadView payload);

  // Receives a `PostInfoHeader` or `DataHeader` packet from the channel.
  static bool Recv(Channel* channel, RequestTracker& outgoing,
                   RequestTracker& incoming, int worker_id,
                   TraceRecorder& tracer);

 private:
  using Header = std::variant<PostInfoHeader, DataHeader>;

  // Deserializes header and returns true iff the header is valid.
  static bool deserialize(const Byte* buf, Header& header);

  // Sends an ack chunk with no payload to the channel.
  static bool sendAck(Channel* channel, DataHeader& chunk);

 private:
  // Receives `PostInfoHeader` or `DataHeader` packet from the reliable stream
  // channel.
  static bool recvStream(Channel* channel, RequestTracker& outgoing,
                         RequestTracker& incoming, int worker_id,
                         TraceRecorder& tracer);
  static bool recvPostInfoStream(Channel* channel, const PostInfoHeader& info,
                                 RequestTracker& incoming);
  static bool recvDataStream(Channel* channel, DataHeader& chunk,
                             RequestTracker& outgoing, RequestTracker& incoming,
                             int worker_id, TraceRecorder& tracer);

  // Receives `PostInfoHeader` or `DataHeader` packet from the message channel.
  static bool recvMsg(Channel* channel, RequestTracker& outgoing,
                      RequestTracker& incoming, int worker_id,
                      TraceRecorder& tracer);
  static bool recvPostInfoMsg(const PostInfoHeader& info,
                              ChunkPayloadView payload,
                              RequestTracker& incoming);
  static bool recvDataMsg(Channel* channel, DataHeader& chunk,
                          ChunkPayloadView payload, RequestTracker& outgoing,
                          RequestTracker& incoming, int worker_id,
                          TraceRecorder& tracer);

  // Discards chunk payload still buffered in the reliable stream channel.
  static bool drainStream(Channel* channel, uint32_t chunk_size);

 private:
  static_assert(assumptions::kNetworkMtuIsAtMostTenKiloBytes);
  static constexpr size_t kTmpBufSize = 10U << 10;
  static_assert(HeaderUtil::kSize < kTmpBufSize);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRANSFER_TRANSFER_H_
