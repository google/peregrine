#ifndef PEREGRINE_SRC_API_TYPES_H_
#define PEREGRINE_SRC_API_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "third_party/gloop/util/intops/strong_int.h"

namespace peregrine {

// `Byte` is an 8-bit unit of data.
using Byte = uint8_t;

// `Handle` uniquely identifies a transport request within one process.
DEFINE_STRONG_INT_TYPE(Handle, uint32_t);

// `Buffer` uniquely identifies a buffer within a transport request.
DEFINE_STRONG_INT_TYPE(Buffer, uint32_t);

// Transport operation.
enum class Op : uint8_t {
  kRead = 1,   // Read from peer
  kWrite = 2,  // Write to peer
};

// Returns a string representation of the transport operation.
std::string ToString(Op op);

inline std::ostream& operator<<(std::ostream& os, const Op op) {
  return os << ToString(op);
}

// Per-peer transport request.
struct Request final {
  // LINT.IfChange
  Op op = Op::kWrite;     // operation type
  Byte* laddr = nullptr;  // address in this local process
  Byte* raddr = nullptr;  // address in the remote peer process
  size_t len = 0;         // buffer length in bytes
  // LINT.ThenChange(src/internal/control/message.proto)

  // Returns true iff the request is valid.
  constexpr bool IsValid() const {
    return laddr != nullptr && raddr != nullptr && len > 0;
  }

  // Returns true iff this request is equal to the request `r`.
  bool operator==(const Request& r) const {
    return op == r.op && laddr == r.laddr && raddr == r.raddr && len == r.len;
  }

  // Returns true iff this request is not equal to the request `r`.
  bool operator!=(const Request& r) const { return !(*this == r); }

  // Returns a string representation of the transport request.
  std::string ToString() const;
};

inline std::ostream& operator<<(std::ostream& os, const Request& r) {
  return os << r.ToString();
}

// Transport operation status.
enum class Status : int {
  kInProgress = 1,
  kSuccess = 0,
  kFailure = -1,
};

// Returns true iff the transport operation is still in progress.
constexpr bool IsInProgress(const Status s) { return s == Status::kInProgress; }

// Returns true iff the transport operation is already completed,
// either successfully or with failure.
constexpr bool IsCompleted(const Status s) { return s != Status::kInProgress; }

// Returns a string representation of the transport operation status.
std::string ToString(Status s);

inline std::ostream& operator<<(std::ostream& os, const Status s) {
  return os << ToString(s);
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TYPES_H_
