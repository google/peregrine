#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_types.h"
#include "peregrine/src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// A tcp socket based channel: reliable, stream.
// It is thread-compatible but not thread-safe.
class TcpChannel final : public Channel {
 public:
  // Constructor.
  explicit TcpChannel(std::unique_ptr<TcpSocket> socket)
      : socket_(std::move(socket)) {
    DCHECK_NE(socket_, nullptr);
    DCHECK(socket_->IsValid());
    DCHECK(socket_->IsBlocking());
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override { return ChannelType::kTCP; }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t Write(absl::Span<const IoVec> iovecs) override;

  // Reads exactly `length(iovecs)` bytes of data from the channel into the
  // buffers. Returns the number of bytes actually read if successful.
  // Returns 0 if the peer side has closed the connection. Returns -1 on error.
  ssize_t Read(absl::Span<IoVec> iovecs) override;

  // Shuts down the channel. After the channel is shutdown, write calls will
  // return -1, so no more data can be injected into the channel. Read calls
  // will continue to read the remaining data in the channel, if any.
  void Shutdown() override { socket_->Shutdown(); }

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<TcpSocket> socket_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_
