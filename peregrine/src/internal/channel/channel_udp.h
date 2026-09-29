#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/socket/socket_udp.h"

namespace peregrine::internal {

// A udp socket based channel: unreliable, message.
// It is thread-compatible but not thread-safe.
class UdpChannel final : public Channel {
 public:
  // Constructor.
  explicit UdpChannel(std::unique_ptr<UdpSocket> socket)
      : socket_(std::move(socket)) {
    DCHECK_NE(socket_, nullptr);
    DCHECK(socket_->IsValid());
    DCHECK(socket_->IsBlocking());
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    constexpr ChannelType t = ChannelType::kUDP;
    static_assert(t.IsLossyChannel());
    static_assert(t.IsMessageChannel());
    static_assert(t.IsTwoSidedChannel());
    static_assert(t.IsRealChannel());
    return t;
  }

  // Writes data from the `iovecs` buffers to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t Write(absl::Span<const IoVec> iovecs) override;

  // Reads one message of up to `length(iovecs)` bytes from the channel into
  // the buffers. Returns the number of bytes actually read if successful.
  // Returns 0 if the received packet has no payload. Returns -1 on error.
  ssize_t Read(absl::Span<IoVec> iovecs) override;

  // Shuts down the channel. After the channel is shutdown, write calls will
  // return -1, so no more data can be injected into the channel. Read calls
  // will continue to read the remaining data in the channel, if any.
  void Shutdown() override { socket_->Shutdown(); }

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<UdpSocket> socket_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
