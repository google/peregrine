#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_

#include <ostream>
#include <string>

#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"

namespace peregrine::internal {

// An interface that specifies a channel abstraction. It is used for
// communications between two endpoints.
class Channel {
 public:
  // Destructor.
  virtual ~Channel() = default;

  // Returns the channel type.
  virtual ChannelType Type() const = 0;

  // Returns whether the channel is blocking or non-blocking.
  virtual bool IsBlocking() const = 0;

  // Returns the underlying file descriptor (>= 0) for the channel.
  // Returns -1 if the channel has none.
  // Note: the channel owns the file descriptor; callers must not close it.
  virtual fd_t GetFd() const { return fd_t(-1); }

  // Returns the peer endpoint of the channel.
  virtual Endpoint GetPeer() const { return peer_; }

  // Sets the peer endpoint of the channel.
  virtual void SetPeer(const Endpoint& peer) { peer_ = peer; }

  // Writes the remaining buffers of the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful, and advances
  // the `iovecs` by the same number of bytes. Zero byte means no data has been
  // written due to non-error reasons. Returns -1 on error, which means the
  // channel should not be used anymore.
  virtual ssize_t Write(IoVecCursor& iovecs) = 0;

  // Reads data from the channel into the remaining buffers of the `iovecs`.
  // For stream channel, it reads exactly (or up to) `iovecs.RemainingBytes()`
  // of data if the channel is blocking (or non-blocking). For message channel,
  // it reads one message of up to `iovecs.RemainingBytes()` of data.
  // Returns the number of bytes actually read if successful, and advances the
  // `iovecs` by the same number of bytes.
  // For stream channel, returns 0 if the peer side has closed the connection.
  // For message channel, returns 0 if the received packet has no payload.
  // Returns -1 on error, which means the channel should not be used anymore.
  virtual ssize_t Read(IoVecCursor& iovecs) = 0;

  // Shuts down the channel. After the channel is shutdown, write calls will
  // return -1, so no more data can be injected into the channel. Read calls
  // will continue to read the remaining data in the channel, if any.
  virtual void Shutdown() = 0;

  // Returns a string representation for the channel.
  virtual std::string ToString() const = 0;

 private:
  Endpoint peer_;
};

inline std::ostream& operator<<(std::ostream& os, const Channel& c) {
  return os << "peer: " << c.GetPeer() << ", " << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_H_
