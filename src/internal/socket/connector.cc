#include "src/internal/socket/connector.h"

#include <cstdint>
#include <memory>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/psp/tcp_psp_helper.h"
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

absl::StatusOr<PspToken> TcpConnector::AcquireRxSpiAndKey(
    const TcpSocket& socket) {
  return peregrine::internal::AcquireRxSpiAndKey(socket.fd());
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

bool TcpConnector::PspConnect(TcpSocket& socket, const Endpoint& peer,
                              const PspToken& peer_token,
                              const PspToken& self_token) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(socket.IsBlocking());
  DCHECK(!socket.IsConnected());

  const absl::Status status = SetTxSpiAndKey(socket.fd(), peer_token);
  if (!status.ok()) {
    LOG(WARNING) << "failed to set Tx SPI and key: " << status;
    return false;
  }

  if (!socket.Connect(peer)) {
    return false;
  }

  const absl::StatusOr<uint32_t> spi = GetInitialRxSpi(socket.fd());
  if (!spi.ok() || *spi != self_token.spi) {
    LOG(WARNING) << "failed to verify negotiated Rx SPI on socket: "
                 << socket.fd();
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
