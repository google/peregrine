#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/types/span.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

// An enum that specifies the type of a channel.
enum class ChannelType : uint8_t {
  kReliableStream,     // e.g., tcp
  kUnreliableMessage,  // e.g., udp
};

// Returns true iff the channel type is reliable stream.
constexpr bool IsReliableStream(ChannelType type) {
  return type == ChannelType::kReliableStream;
}

// Returns true iff the channel type is unreliable message.
constexpr bool IsUnreliableMessage(ChannelType type) {
  return type == ChannelType::kUnreliableMessage;
}

// An interface that specifies a channel abstraction. It is used for
// communications between two endpoints.
class Channel {
 public:
  virtual ~Channel() = default;

  // Returns the channel type.
  virtual ChannelType Type() const = 0;

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully. Otherwise,
  // returns false.
  virtual bool Write(absl::Span<const IoVec> iovecs) = 0;

  // Reads data into the `buf` from the the channel. For stream/message channel,
  // it reads exactly/at most `len` bytes, respectively. Returns the number of
  // bytes actually read if successful, or -1 otherwise.
  virtual ssize_t Read(Byte* buf, size_t len) = 0;

  // Returns a string representation for the channel.
  virtual std::string ToString() const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Channel& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
