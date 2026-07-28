#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_

#include <memory>
#include <utility>

#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"

namespace peregrine::internal::testing {

// Creates a connected tcp socket pair in the given address family.
std::pair<std::unique_ptr<TcpSocket>, std::unique_ptr<TcpSocket>>
CreateTcpSocketPair(int family);

// Creates a connected udp socket pair in the given address family.
std::pair<std::unique_ptr<UdpSocket>, std::unique_ptr<UdpSocket>>
CreateUdpSocketPair(int family);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
