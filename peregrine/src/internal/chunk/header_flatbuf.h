#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_generated.h"

namespace peregrine::internal {

// This utility class implements fixed-size flatbuffer (de)serialization for
// `HeaderBase`, `PostInfoHeader`, `DataHeader`, and `RequestInfo`.
// It is thread-safe since it has no data members.
class HeaderUtil final {
 public:
  static constexpr size_t kSize = 64;
  static constexpr size_t kRequestInfoSize = kSerializedRequestInfoSize;
  static_assert(sizeof(flatbuf::HeaderBase) == 12);
  static_assert(sizeof(flatbuf::ChunkTraceWire) == 16);
  static_assert(sizeof(flatbuf::PostInfoHeader) == kSize);
  static_assert(sizeof(flatbuf::DataHeader) == kSize);
  static_assert(sizeof(flatbuf::Recipe) == 40);
  static_assert(sizeof(flatbuf::RequestInfo) == kRequestInfoSize);

  // Serializes a `PostInfoHeader` or `DataHeader` to a 64-byte flatbuffer
  // string.
  static std::string Serialize(const PostInfoHeader& header, uint8_t ver = 1);
  static std::string Serialize(const DataHeader& header, uint8_t ver = 1);

  // Parses the common `HeaderBase` prefix from a 64-byte flatbuffer header
  // string (e.g., to inspect `opcode`, `handle`, and `len` before dispatching).
  static bool Deserialize(std::string_view s, HeaderBase& base);

  // Parses a `PostInfoHeader` or `DataHeader` from a 64-byte flatbuffer string.
  static bool Deserialize(std::string_view s, PostInfoHeader& header);
  static bool Deserialize(std::string_view s, DataHeader& header);

  // Serializes/deserializes a single `RequestInfo` (128 bytes) or a non-empty
  // sequence of `RequestInfo` structs (`N * 128` bytes, the `POST_INFO`
  // payload).
  static std::string Serialize(const RequestInfo& req_info);
  static bool Deserialize(std::string_view s, RequestInfo& req_info);
  static std::string Serialize(absl::Span<const RequestInfo> requests);
  static bool Deserialize(std::string_view s,
                          std::vector<RequestInfo>& requests);

 private:
  static constexpr uint16_t kMagic = 0x4750;  // 'PG' in little-endian order

 private:
  static std::string serializeV1(const PostInfoHeader& header);
  static std::string serializeV1(const DataHeader& header);

  static bool deserializeV1(const flatbuf::HeaderBase& fb_base,
                            HeaderBase& base);
  static bool deserializeV1(const flatbuf::PostInfoHeader& h,
                            PostInfoHeader& header);
  static bool deserializeV1(const flatbuf::DataHeader& h, DataHeader& header);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_FLATBUF_H_
