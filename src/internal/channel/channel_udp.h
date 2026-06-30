#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_

#include <sys/socket.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
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

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully, or false otherwise.
  bool Write(absl::Span<const IoVec> iovecs) override;

  // Reads a message of at most `len` bytes into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if the
  // received packet has no payload. Returns -1 on error.
  ssize_t Read(Byte* buf, size_t len) override {
    return socket_->Recv(buf, len);
  }

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override { socket_->Shutdown(); }

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<UdpSocket> socket_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UDP_H_
