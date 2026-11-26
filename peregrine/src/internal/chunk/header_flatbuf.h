#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_generated.h"

namespace peregrine::internal {

// This utility class implements fixed-size flatbuffer (de)serialization for
// `HeaderBase`, `PostInfoHeader`, `DataHeader`, and `RequestInfo`.
// It is thread-safe since it has no data members.
class HeaderUtil final {
  static_assert(assumptions::kChunkHeaderSerializesTo64BytesFixedSizeFlatBuf);
  static_assert(assumptions::kChunkHeaderHasBackwardForwardCompatibilityIssue);

 public:
  static constexpr size_t kSize = 64;
  static constexpr size_t kRequestInfoSize = kSerializedRequestInfoSize;
  static_assert(sizeof(flatbuf::HeaderBase) == 12);
  static_assert(sizeof(flatbuf::Endpoint) == 20);
  static_assert(sizeof(flatbuf::ChunkTraceWire) == 16);
  static_assert(sizeof(flatbuf::PostInfoHeader) == kSize);
  static_assert(sizeof(flatbuf::DataHeader) == kSize);
  static_assert(sizeof(flatbuf::FailureHeader) == kSize);
  static_assert(sizeof(flatbuf::Recipe) == 40);
  static_assert(sizeof(flatbuf::RequestInfo) == kRequestInfoSize);

  // Serializes a header into a caller-supplied 64-byte buffer without
  // allocating.
  static void Serialize(const PostInfoHeader& header,
                        std::array<Byte, kSize>& out, uint8_t ver = 1);
  static void Serialize(const DataHeader& header, std::array<Byte, kSize>& out,
                        uint8_t ver = 1);
  static void Serialize(const FailureHeader& header,
                        std::array<Byte, kSize>& out, uint8_t ver = 1);

  // Serializes a `PostInfoHeader`, `DataHeader`, or `FailureHeader` to a
  // 64-byte flatbuffer string.
  static std::string Serialize(const PostInfoHeader& header, uint8_t ver = 1);
  static std::string Serialize(const DataHeader& header, uint8_t ver = 1);
  static std::string Serialize(const FailureHeader& header, uint8_t ver = 1);

  // Parses the common `HeaderBase` prefix from a 64-byte flatbuffer header
  // string (e.g., to inspect `opcode`, `handle`, and `len` before dispatching).
  static bool Deserialize(std::string_view s, HeaderBase& base);

  // Parses a `PostInfoHeader`, `DataHeader`, or `FailureHeader` from a 64-byte
  // flatbuffer string.
  static bool Deserialize(std::string_view s, PostInfoHeader& header);
  static bool Deserialize(std::string_view s, DataHeader& header);
  static bool Deserialize(std::string_view s, FailureHeader& header);

  // Serializes/deserializes a single `RequestInfo` (128 bytes) or a non-empty
  // sequence of `RequestInfo` structs (`N * 128` bytes, the `POST_INFO`
  // payload).
  static std::string Serialize(const RequestInfo& req_info);
  static bool Deserialize(std::string_view s, RequestInfo& req_info);
  static bool Serialize(absl::Span<const RequestInfo> requests,
                        absl::Span<Byte> out);
  static std::string Serialize(absl::Span<const RequestInfo> requests);
  static bool Deserialize(std::string_view s,
                          std::vector<RequestInfo>& requests);

  // Returns the serialized byte size of a post-info header followed by the
  // given number of request descriptors, or nullopt if the count is zero or
  // exceeds the maximum allowed payload size.
  static std::optional<size_t> PostInfoFrameSize(size_t num_reqs);

  // Serializes a complete post-info frame (header + request descriptors) into
  // a caller-supplied buffer without allocating. Returns false if the request
  // count or output buffer size is invalid.
  static bool SerializePostInfo(Handle handle, const Endpoint& peer,
                                const Metadata& metadata,
                                absl::Span<const RequestInfo> requests,
                                absl::Span<Byte> out, uint8_t ver = 1);

 private:
  static constexpr uint16_t kMagic = 0x4750;  // 'PG' in little-endian order

 private:
  static void serializeV1(const PostInfoHeader& header,
                          std::array<Byte, kSize>& out);
  static void serializeV1(const DataHeader& header,
                          std::array<Byte, kSize>& out);
  static void serializeV1(const FailureHeader& header,
                          std::array<Byte, kSize>& out);

  static bool deserializeV1(const flatbuf::HeaderBase& fb_base,
                            HeaderBase& base);
  static bool deserializeV1(const flatbuf::PostInfoHeader& h,
                            PostInfoHeader& header);
  static bool deserializeV1(const flatbuf::DataHeader& h, DataHeader& header);
  static bool deserializeV1(const flatbuf::FailureHeader& h,
                            FailureHeader& header);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_
