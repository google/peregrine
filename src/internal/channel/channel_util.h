#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_

#include <memory>
#include <utility>
#include <vector>

#include "src/internal/base/endpoint.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_tcp.h"
#include "src/internal/channel/channel_udp.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"

namespace peregrine::internal {

// Creates a tcp channel.
inline std::unique_ptr<Channel> CreateTcpChannel(
    std::unique_ptr<TcpSocket> socket) {
  return std::make_unique<TcpChannel>(std::move(socket));
}

// Creates a udp channel.
inline std::unique_ptr<Channel> CreateUdpChannel(
    std::unique_ptr<UdpSocket> socket) {
  return std::make_unique<UdpChannel>(std::move(socket));
}

using Channels = std::vector<std::unique_ptr<Channel>>;

// Creates a number of channels connected to the peer.
Channels Create(const Endpoint& peer, int num_channels);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_
