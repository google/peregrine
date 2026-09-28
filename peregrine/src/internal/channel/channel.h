#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_

#include <ostream>
#include <string>

#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel_types.h"

namespace peregrine::internal {

// An interface that specifies a channel abstraction. It is used for
// communications between two endpoints.
class Channel {
 public:
  // Destructor.
  virtual ~Channel() = default;

  // Returns the channel type.
  virtual ChannelType Type() const = 0;

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  virtual ssize_t Write(absl::Span<const IoVec> iovecs) = 0;

  // Reads data from the channel into the `iovecs` buffers.
  // For stream channel, it reads exactly `length(iovecs)` bytes of data.
  // For message channel, it reads one message of up to `length(iovecs)` bytes.
  // Returns the number of bytes actually read if successful.
  // For stream channel, returns 0 if the peer side has closed the connection.
  // For message channel, returns 0 if the received packet has no payload.
  // Returns -1 on error.
  virtual ssize_t Read(absl::Span<IoVec> iovecs) = 0;

  // Shuts down the channel. After the channel is shutdown, write calls will
  // return -1, so no more data can be injected into the channel. Read calls
  // will continue to read the remaining data in the channel, if any.
  virtual void Shutdown() = 0;

  // Returns a string representation for the channel.
  virtual std::string ToString() const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Channel& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
