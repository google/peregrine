#include "src/internal/socket/connector.h"

#include <memory>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpConnector::Create(const Endpoint& peer,
                                                const Endpoint& local) {
  DCHECK(peer.HasNonzeroIpPort());

  const int family = peer.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  if (!local.HasZeroIpAddr() && !socket->Bind(local)) {
    return nullptr;
  }

  DCHECK(socket->IsBlocking());
  if (!socket->Connect(peer)) {
    return nullptr;
  }

  DCHECK(socket->IsConnected());
  LOG(INFO) << "made " << *socket;
  return socket;
}

}  // namespace peregrine::internal
