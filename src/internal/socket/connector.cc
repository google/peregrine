#include "src/internal/socket/connector.h"

#include <memory>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpConnector::CreateUnconnected(
    const Endpoint& peer, const Endpoint& local) {
  DCHECK(peer.HasNonzeroIpPort());

  const int family = peer.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  if (!local.HasZeroIpAddr() && !socket->Bind(local)) {
    return nullptr;
  }

  return socket;
}

bool TcpConnector::Connect(TcpSocket& socket, const Endpoint& peer) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(!socket.IsConnected());
  DCHECK(socket.IsBlocking());
  if (!socket.Connect(peer)) {
    return false;
  }

  DCHECK(socket.IsConnected());
  LOG(INFO) << "made " << socket;
  return true;
}

std::unique_ptr<TcpSocket> TcpConnector::Create(const Endpoint& peer,
                                                const Endpoint& local) {
  std::unique_ptr<TcpSocket> socket = CreateUnconnected(peer, local);
  if (socket == nullptr) {
    return nullptr;
  }

  if (!Connect(*socket, peer)) {
    return nullptr;
  }

  return socket;
}

}  // namespace peregrine::internal
