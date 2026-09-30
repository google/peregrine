#ifndef PEREGRINE_SRC_INTERNAL_BASE_CONSTANTS_H_
#define PEREGRINE_SRC_INTERNAL_BASE_CONSTANTS_H_

namespace peregrine::internal {

// For TCP listening sockets.
// Make sure the following sysctl values are big enough.
// - net.core.somaxconn
// - net.core.netdev_max_backlog
// - net.ipv4.tcp_max_syn_backlog
inline constexpr int kTcpListenBacklog = 8192;

// For epoll.
inline constexpr int kEpollMaxNumEvents = 128;
inline constexpr int kEpollWaitTimeoutMs = 50;
inline constexpr int kEpollMaxAcceptsPerEvent = 256;

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_CONSTANTS_H_
