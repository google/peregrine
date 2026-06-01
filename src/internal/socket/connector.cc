#include "src/internal/socket/connector.h"

#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kConnector = "tcp connector ";
}  // namespace

std::unique_ptr<TcpSocket> TcpConnector::Create(const Endpoint& peer) {
  DCHECK(peer.IsValid());

  const int family = peer.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  DCHECK(socket->IsBlocking());
  if (!socket->Connect(peer)) {
    return nullptr;
  }

  LOG(INFO) << kConnector << "made " << *socket;
  DCHECK(socket->IsConnected());
  return socket;
}

}  // namespace peregrine::internal
