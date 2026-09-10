#ifndef PEREGRINE_SRC_API_TRANSPORT_TYPES_H_
#define PEREGRINE_SRC_API_TRANSPORT_TYPES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/types/span.h"
#include "src/api/strong_int.h"

namespace peregrine {

// `Byte` is an 8-bit unit of data.
using Byte = uint8_t;

// `Handle` uniquely identifies a transport request within one process.
DEFINE_STRONG_INT_TYPE(Handle, uint32_t);

// Transport operation.
enum class Op : uint8_t {
  kRead = 1,   // Read from peer
  kWrite = 2,  // Write to peer
};

// Supported underlying data plane transports.
enum class TransportType : uint8_t {
  kTcp = 1,
  kRdma = 2,
};

// Per-peer transport request.
struct Request final {
  // LINT.IfChange
  Op op = Op::kWrite;     // operation type
  Byte* laddr = nullptr;  // address in this local process
  Byte* raddr = nullptr;  // address in the remote peer process
  size_t len = 0;         // buffer length in bytes
  uint32_t rkey = 0;      // remote memory region key (for RDMA operations)
  // LINT.ThenChange(../internal/control/message_internal.proto)

  // Returns true iff the request is valid.
  constexpr bool IsValid() const {
    return laddr != nullptr && raddr != nullptr && 1 <= len;
  }

  // Returns true iff the requests are equal.
  friend bool operator==(const Request& a, const Request& b) {
    return a.op == b.op && a.laddr == b.laddr && a.raddr == b.raddr &&
           a.len == b.len && a.rkey == b.rkey;
  }

  // Returns a string representation of the transport request.
  std::string ToString() const;
};

// Returns true iff the requests has at least one request and all are valid.
inline bool IsValid(absl::Span<const Request> requests) {
  return !requests.empty() &&
         std::all_of(requests.begin(), requests.end(),
                     [](const Request& r) { return r.IsValid(); });
}

// Transport operation status.
enum class Status : int {
  kNotFound = 2,
  kInProgress = 1,
  kSuccess = 0,
  kFailure = -1,
};

// Returns true iff the transport operation is still in progress.
constexpr bool IsInProgress(const Status s) { return s == Status::kInProgress; }

// Returns true iff the transport operation is already completed,
// either successfully or with failure.
constexpr bool IsCompleted(const Status s) { return static_cast<int>(s) <= 0; }

// Returns a string representation of the transport operation status.
std::string ToString(Status s);

// Returns a string representation of the transport operation.
std::string ToString(Op op);

inline std::ostream& operator<<(std::ostream& os, const Op op) {
  return os << ToString(op);
}
inline std::ostream& operator<<(std::ostream& os, const Request& r) {
  return os << r.ToString();
}

inline std::ostream& operator<<(std::ostream& os, const Status s) {
  return os << ToString(s);
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_TYPES_H_
