#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/trace/chunk_trace_wire.h"

namespace peregrine::internal {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);

// `ChunkPayloadView` defines a read-only view to the chunk raw data bytes.
static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
using ChunkPayloadView = absl::Span<const Byte>;
static_assert(sizeof(ChunkPayloadView) == 16);

// Wire packet type carried in the common `HeaderBase` prefix.
enum class Opcode : uint8_t {
  // LINT.IfChange(opcode)
  kPostInfo = 1,  // control header followed by `RequestInfo` entries
  kData = 2,      // data chunk header (len > 0) or ACK (len == 0)
  // LINT.ThenChange(header.fbs:opcode)
};

// Wire size of a single serialized `RequestInfo` entry in a `PostInfo` payload.
inline constexpr size_t kSerializedRequestInfoSize = 128;

// Common prefix fields shared by all 64-byte chunk headers (`PostInfoHeader`
// and `DataHeader`).
struct alignas(8) HeaderBase {
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  static_assert(assumptions::kChunkHeaderHasBackwardForwardCompatibilityIssue);
  // LINT.IfChange(header_base)
  Opcode opcode;  // REQUIRED: packet type (`kPostInfo` or `kData`)
  Handle handle;  // REQUIRED: transport batch handle id
  uint32_t len;   // REQUIRED: payload length in bytes following header
  // LINT.ThenChange(header.fbs:header_base)

  // Returns true iff this header is a `PostInfo` control header.
  bool IsPostInfo() const {
    return opcode == Opcode::kPostInfo && len > 0 &&
           len % kSerializedRequestInfoSize == 0;
  }

  // Returns true iff this header is a `Data` chunk header carrying payload.
  bool IsData() const { return opcode == Opcode::kData && len > 0; }

  // Returns true iff this header is a `Data` chunk acknowledgment (zero
  // payload).
  bool IsAck() const { return opcode == Opcode::kData && len == 0; }

  // Returns true iff the base header fields are valid.
  bool IsValid() const { return IsPostInfo() || IsData() || IsAck(); }

  // Returns a string representation for debugging/logging.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const HeaderBase& a, const HeaderBase& b) = default;
};
static_assert(sizeof(HeaderBase) == 16);

inline std::ostream& operator<<(std::ostream& os, const HeaderBase& base) {
  return os << base.ToString();
}

// Control header for a `Post()` batch. Its wire payload is a contiguous
// sequence of serialized 128-byte `RequestInfo` structs (`len` bytes total).
struct alignas(8) PostInfoHeader final : public HeaderBase {
  static constexpr size_t kMetadataSize = 32;

  // LINT.IfChange(post_info_header)
  // OPTIONAL: opaque application-level metadata for the entire `Post()` batch,
  // delivered to the receiver on batch completion.
  std::array<Byte, kMetadataSize> metadata;
  // LINT.ThenChange(header.fbs:post_info_header)

  // Returns true iff this `PostInfoHeader` is valid.
  bool IsValid() const { return IsPostInfo(); }

  // Returns a string representation for debugging/logging.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const PostInfoHeader& a,
                         const PostInfoHeader& b) = default;
};
static_assert(sizeof(PostInfoHeader) == 48);

inline std::ostream& operator<<(std::ostream& os,
                                const PostInfoHeader& header) {
  return os << header.ToString();
}

inline bool IsMatch(const PostInfoHeader& header, ChunkPayloadView payload) {
  DCHECK(header.IsValid());
  return header.len == payload.size();
}

// Per-chunk header for data transfers (`len > 0`) and chunk ACKs (`len == 0`).
struct alignas(8) DataHeader final : public HeaderBase {
  // LINT.IfChange(data_header)
  ReqId reqid;    // REQUIRED: request id within the handle
  chunk_t index;  // REQUIRED: 0-based chunk index within the request
  // REQUIRED: byte offset of this chunk relative to the request's base buffer
  // (e.g., `index * chunk_size` for contiguous transfers, or the logical offset
  // within the transfer for non-contiguous recipes).
  uint64_t byte_offset;
  addr_t raddr;          // REQUIRED: target chunk address on the remote peer
  ChunkTraceWire trace;  // OPTIONAL: per-chunk trace metadata
  // LINT.ThenChange(header.fbs:data_header)

  // Returns true iff this `DataHeader` is valid.
  bool IsValid() const { return IsData() || IsAck(); }

  // Returns the destination memory address for this chunk.
  Byte* DstAddr() const { return reinterpret_cast<Byte*>(raddr.value()); }

  // Returns a string representation for debugging/logging.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const DataHeader& a, const DataHeader& b) = default;
};
static_assert(sizeof(DataHeader) == 56);

inline std::ostream& operator<<(std::ostream& os, const DataHeader& header) {
  return os << header.ToString();
}

inline bool IsMatch(const DataHeader& header, ChunkPayloadView payload) {
  DCHECK(header.IsValid());
  return header.len == payload.size();
}

// Identifies the data layout recipe used to reconstruct or slice a request's
// buffer on the remote peer.
enum class RecipeType : uint8_t {
  // LINT.IfChange(recipe_type)
  kContiguous = 1,  // contiguous range `[raddr, raddr + total_len)`
  // LINT.ThenChange(header.fbs:recipe_type)
};

// Describes the memory layout recipe and its fixed-size parameters for a
// `RequestInfo`.
struct alignas(8) Recipe final {
  static constexpr size_t kParamsSize = 32;

  // LINT.IfChange(recipe)
  RecipeType type;  // REQUIRED: layout recipe type
  // OPTIONAL: 8-byte-aligned recipe-specific parameters.
  alignas(8) std::array<Byte, kParamsSize> params;
  // LINT.ThenChange(header.fbs:recipe)

  // Returns true iff the recipe type is valid.
  bool IsValid() const { return type == RecipeType::kContiguous; }

  // Equality operator.
  friend bool operator==(const Recipe& a, const Recipe& b) = default;
};
static_assert(sizeof(Recipe) == 40);

// Per-request descriptor carried in the payload of a `POST_INFO` packet,
// advertising a single `Read` or `Write` request within a `Post()` batch to the
// receiving peer before data chunks arrive.
struct alignas(8) RequestInfo final {
  static constexpr size_t kMetadataSize = 32;

  // LINT.IfChange(request_info)
  ReqId reqid;         // REQUIRED: unique request id within the handle
  uint32_t nchunks;    // REQUIRED: total #chunks for this request
  addr_t raddr;        // REQUIRED: base buffer address on the remote peer
  uint64_t total_len;  // REQUIRED: total transfer length in bytes
  Op op;               // REQUIRED: operation type (`kRead` or `kWrite`)
  Recipe recipe;       // REQUIRED: data layout recipe on the remote peer
  // OPTIONAL: opaque per-request application metadata delivered to the
  // receiver.
  std::array<Byte, kMetadataSize> metadata;
  // LINT.ThenChange(header.fbs:request_info)

  // Returns true iff this `RequestInfo` is valid.
  bool IsValid() const {
    return (op == Op::kRead || op == Op::kWrite) && raddr.value() != 0 &&
           total_len > 0 && nchunks > 0 && recipe.IsValid();
  }

  // Returns a string representation for debugging/logging.
  std::string ToString() const;

  // Equality operator.
  friend bool operator==(const RequestInfo& a, const RequestInfo& b) = default;
};
static_assert(sizeof(RequestInfo) == 104);

inline std::ostream& operator<<(std::ostream& os, const RequestInfo& info) {
  return os << info.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_HEADER_H_
