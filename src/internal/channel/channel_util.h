#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "src/internal/base/endpoint.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_rdma.h"
#include "src/internal/channel/channel_tcp.h"
#include "src/internal/channel/channel_udp.h"
#include "src/internal/rdma/rdma_queue_pair.h"
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

// Creates an rdma channel.
inline std::unique_ptr<Channel> CreateRdmaChannel(
    std::unique_ptr<RdmaQueuePair> qp, uint32_t lkey = 0, uint32_t rkey = 0) {
  return std::make_unique<RdmaChannel>(std::move(qp), lkey, rkey);
}

// Creates `n` channels connected to the `peer`.
std::vector<std::unique_ptr<Channel>> Create(const Endpoint& peer, int n);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_UTIL_H_
