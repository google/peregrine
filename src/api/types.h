#ifndef PEREGRINE_SRC_API_TYPES_H_
#define PEREGRINE_SRC_API_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/strings/string_view.h"
#include "util/intops/strong_int.h"

namespace peregrine {

// `Endpoint` uniquely identifies a process.
// It is often sufficient to use a string of
//
//   - `ip:port` ("127.0.0.1:9999"), or
//   - `hostname:port` ("localhost:9999")
//
// to represent such a process, whose control channel listens on it. Even the
// `port` can be omitted if it is well-known by the communicating processes.
using Endpoint = absl::string_view;
using EndpointStr = std::string;

// `Byte` is an 8-bit unit of data.
using Byte = uint8_t;

// `Handle` uniquely identifies a transport request within one process.
DEFINE_STRONG_INT_TYPE(Handle, uint32_t);

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

// Per-peer transport request.
struct Request final {
  Op op = Op::kWrite;
  Byte* laddr = nullptr;  // in this local process
  Byte* raddr = nullptr;  // in the remote peer process
  size_t len = 0;

  // Returns true iff the request is valid.
  constexpr bool IsValid() const {
    return laddr != nullptr && raddr != nullptr && len > 0;
  }

  // Returns a string representation of the transport request.
  std::string ToString() const;
};

inline std::ostream& operator<<(std::ostream& os, const Request& r) {
  return os << r.ToString();
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TYPES_H_
