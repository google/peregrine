#include "src/internal/util/test_util.h"

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {

port_t TestOnly_FindFreeTcpPort(int family) {
  const port_t port = util::FindFreePort(family, /*kTcp=*/true);
  CHECK_GT(port, 0);  // Crash OK
  return port;
}

port_t TestOnly_FindFreeUdpPort(int family) {
  const port_t port = util::FindFreePort(family, /*kTcp=*/false);
  CHECK_GT(port, 0);  // Crash OK
  return port;
}

std::unique_ptr<TcpSocket> TestOnly_CreateTcpSocket(int family) {
  auto maybe_socket = TcpSocket::Create(family);
  CHECK_OK(maybe_socket);  // Crash OK

  auto socket = std::move(maybe_socket).value();
  DCHECK_EQ(socket->family(), family);
  DCHECK(socket->IsValid());
  DCHECK(socket->IsBlocking());
  DCHECK(!socket->IsConnected());
  return socket;
}

std::unique_ptr<UdpSocket> TestOnly_CreateUdpSocket(int family) {
  auto maybe_socket = UdpSocket::Create(family);
  CHECK_OK(maybe_socket);  // Crash OK

  auto socket = std::move(maybe_socket).value();
  DCHECK_EQ(socket->family(), family);
  DCHECK(socket->IsValid());
  DCHECK(socket->IsBlocking());
  DCHECK(!socket->IsConnected());
  return socket;
}

}  // namespace peregrine::internal::testing
