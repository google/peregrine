#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
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

// Returns the number of open socket file descriptors in the current process.
size_t CountOpenSocketFds();

// Creates an IoVecCursor by splitting the given data into `n` parts.
std::unique_ptr<IoVecCursor> CreateIoVecCursor(absl::Span<Byte> data,
                                               int splits);

// Creates an IoVecCursor by splitting the given data into `n` parts.
inline std::unique_ptr<IoVecCursor> CreateIoVecCursor(std::vector<Byte>& data,
                                                      int splits) {
  return CreateIoVecCursor(absl::MakeSpan(data), splits);
}

std::unique_ptr<IoVecCursor> CreateIoVecCursor(std::vector<Byte>&& data,
                                               int splits) = delete;

// A loopback listening socket whose accept queue is fully filled.
// As a result, further connection attempts to it will time out.
// This class is thread-compatible but not thread-safe.
class BlackHoleListener {
 public:
  // Constructor.
  BlackHoleListener(int family, int timeout_ms);

  // Returns true iff the listener accept queue is full.
  bool IsFull() const { return full_; }

  // Returns the endpoint of the listener.
  Endpoint GetEndpoint() const { return endpoint_; }

 private:
  // Fills the accept queue of the listener.
  // Returns the number of connections accepted and kept open.
  size_t fillAcceptQueue(int family, int timeout_ms);

 private:
  bool full_;
  Endpoint endpoint_;
  std::unique_ptr<TcpSocket> listener_;
  std::vector<std::unique_ptr<TcpSocket>> connectors_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TEST_UTIL_H_
