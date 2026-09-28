#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_

#include <memory>
#include <utility>

#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_udp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal::testing {

// Returns one incoming socket from the tcp manager.
std::unique_ptr<TcpSocket> GetOneIncomingSocket(TcpManager& mgr);

// Returns one outgoing socket from the tcp manager.
std::unique_ptr<TcpSocket> GetOneOutgoingSocket(TcpManager& mgr);

// Creates a connected tcp socket pair.
std::pair<std::unique_ptr<TcpSocket>, std::unique_ptr<TcpSocket>>
CreateTcpSocketPair(int family, bool blocking);

// Creates a connected udp socket pair.
std::pair<std::unique_ptr<UdpSocket>, std::unique_ptr<UdpSocket>>
CreateUdpSocketPair(int family, bool blocking);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
