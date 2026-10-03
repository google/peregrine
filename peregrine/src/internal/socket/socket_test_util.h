#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
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

// Creates an IoVecCursor by splitting the given data into `n` parts.
std::unique_ptr<IoVecCursor> CreateIoVecCursor(absl::Span<Byte> data, int n);

// Creates an IoVecCursor by splitting the given data into `n` parts.
inline std::unique_ptr<IoVecCursor> CreateIoVecCursor(std::vector<Byte>& data,
                                                      int splits) {
  return CreateIoVecCursor(absl::MakeSpan(data), splits);
}

std::unique_ptr<IoVecCursor> CreateIoVecCursor(std::vector<Byte>&& data,
                                               int splits) = delete;

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
