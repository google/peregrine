#include "src/internal/util/test_util.h"

#include <memory>

#include "absl/log/check.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/util/ipaddr.h"
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
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  CHECK_NE(socket, nullptr);  // Crash OK
  DCHECK_EQ(socket->family(), family);
  DCHECK(socket->IsValid());
  DCHECK(socket->IsBlocking());
  DCHECK(!socket->IsConnected());
  return socket;
}

std::unique_ptr<UdpSocket> TestOnly_CreateUdpSocket(int family) {
  std::unique_ptr<UdpSocket> socket = UdpSocket::Create(family);
  CHECK_NE(socket, nullptr);  // Crash OK
  DCHECK_EQ(socket->family(), family);
  DCHECK(socket->IsValid());
  DCHECK(socket->IsBlocking());
  DCHECK(!socket->IsConnected());
  return socket;
}

Endpoint TestOnly_LocalEndpoint(int family, bool tcp) {
  const util::IpAddr ipaddr = IpLocalhost(family);
  if (tcp) {
    return Endpoint(ipaddr, TestOnly_FindFreeTcpPort(family));
  } else {
    return Endpoint(ipaddr, TestOnly_FindFreeUdpPort(family));
  }
}

HostInfo TestOnly_LocalHostInfo(int family, bool tcp) {
  const util::IpAddr ipaddr = IpLocalhost(family);
  if (tcp) {
    const auto c = Endpoint(ipaddr, TestOnly_FindFreeTcpPort(family));
    return HostInfo{c, {}};
  } else {
    const auto c = Endpoint(ipaddr, TestOnly_FindFreeUdpPort(family));
    return HostInfo{c, {}};
  }
}

}  // namespace peregrine::internal::testing
