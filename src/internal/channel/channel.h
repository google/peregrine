#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_

#include <cstddef>
#include <ostream>
#include <string>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel_types.h"

namespace peregrine::internal {

// An interface that specifies a channel abstraction. It is used for
// communications between two endpoints.
class Channel {
 public:
  virtual ~Channel() = default;

  // Returns the channel type.
  virtual ChannelType Type() const = 0;

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  virtual ssize_t Write(absl::Span<const IoVec> iovecs) = 0;

  // Reads data into the `buf` from the the channel.
  // For stream channel, it reads exactly `len` bytes of data.
  // For message channel, it reads one message of at most `len` bytes.
  // Returns the number of bytes actually read if successful.
  // For stream channel, returns 0 if the peer side has closed the connection.
  // For message channel, returns 0 if the received packet has no payload.
  // Returns -1 on error.
  virtual ssize_t Read(Byte* buf, size_t len) = 0;

  // Shuts down the channel so no more read/write calls.
  virtual void Shutdown() = 0;

  // Returns a string representation for the channel.
  virtual std::string ToString() const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Channel& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
