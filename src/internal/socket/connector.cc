#include "src/internal/socket/connector.h"

#include <memory>

#include "absl/base/optimization.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/psp/psp_util.h"
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

std::unique_ptr<TcpSocket> TcpConnector::CreatePsp(
    const Endpoint& peer_target, const Endpoint& peer_control,
    PspTokenExchangeFunc& psp_exchange_func, const Endpoint& local) {
  DCHECK(peer_target.HasNonzeroIpPort());
  DCHECK(peer_control.HasNonzeroIpPort());
  DCHECK_NE(psp_exchange_func, nullptr);

  const int family = peer_target.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  if (!local.HasZeroIpAddr() && !socket->Bind(local)) {
    return nullptr;
  }

  const fd_t fd = socket->fd();
  const absl::StatusOr<PspToken> self_token = psp::AcquireRxSpiAndKey(fd);
  if (!self_token.ok() || !self_token->IsValid()) {
    LOG(WARNING) << "failed to acquire self rx psp token";
    return nullptr;
  }

  const absl::StatusOr<PspToken> peer_token =
      psp_exchange_func(self_token.value(), peer_target, peer_control);
  if (!peer_token.ok()) {
    LOG(WARNING) << "failed to exchange psp token with peer";
    return nullptr;
  }

  const absl::Status status = psp::SetTxSpiAndKey(fd, peer_token.value());
  if (!status.ok()) {
    LOG(WARNING) << "failed to set tx psp token: " << status;
    return nullptr;
  }

  DCHECK(socket->IsBlocking());
  if (!socket->Connect(peer_target)) {
    return nullptr;
  }

  const absl::StatusOr<Spi> spi = psp::GetInitialRxSpi(fd);
  if (!spi.ok() || (*spi).value() == 0 || *spi != self_token->spi) {
    LOG(WARNING) << "failed to verify negotiated rx psp spi";
    return nullptr;
  }

  DCHECK(socket->IsConnected());
  LOG(INFO) << "made psp " << *socket;
  return socket;
}

}  // namespace peregrine::internal
