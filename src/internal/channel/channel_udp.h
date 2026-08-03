#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/socket/socket_udp.h"

namespace peregrine::internal {

// A udp socket based channel: unreliable, message.
// It is thread-compatible but not thread-safe.
class UdpChannel final : public Channel {
 public:
  // Constructor.
  explicit UdpChannel(std::unique_ptr<UdpSocket> socket)
      : socket_(std::move(socket)) {
    DCHECK_NE(socket_, nullptr);
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kUnreliableMessage;
  }

  // Writes a buffer of `len` bytes to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t Write(const Byte* buf, size_t len) override {
    DCHECK_NE(buf, nullptr);
    DCHECK_GE(len, 1);
    return socket_->Send(buf, len);
  }

  // Writes data from the `iovecs` buffers to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t WriteV(absl::Span<const IoVec> iovecs) override;

  // Reads a message of at most `len` bytes into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if the
  // received packet has no payload. Returns -1 on error.
  ssize_t Read(Byte* buf, size_t len) override {
    DCHECK_NE(buf, nullptr);
    DCHECK_GE(len, 1);
    return socket_->Recv(buf, len);
  }

  // Reads at most `length(iovecs)` bytes into the buffers from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if the
  // received packet has no payload. Returns -1 on error.
  ssize_t ReadV(absl::Span<IoVec> iovecs) override;

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override { socket_->Shutdown(); }

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<UdpSocket> socket_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
