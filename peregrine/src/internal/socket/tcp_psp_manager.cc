#include "peregrine/src/internal/socket/tcp_psp_manager.h"

#include <memory>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/psp/psp_util.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpPspManager::ConnectPsp(
    const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
    PspTokenExchange& psp_token_xchg) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(peer_control.HasNonzeroIpPort());
  DCHECK_NE(psp_token_xchg, nullptr);

  const int family = peer.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/true);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  if (!self.HasZeroIpAddr() && socket->Bind(self)) {
    return nullptr;
  }

  const fd_t fd = socket->fd();
  const absl::StatusOr<PspToken> self_token = psp::AcquireRxSpiAndKey(fd);
  if (!self_token.ok() || !self_token->IsValid()) {
    LOG(WARNING) << "failed to acquire self rx psp token";
    return nullptr;
  }

  const absl::StatusOr<PspToken> peer_token =
      psp_token_xchg(self_token.value(), peer, peer_control);
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
  if (socket->Connect(peer)) {
    return nullptr;
  }

  const absl::StatusOr<Spi> spi = psp::GetInitialRxSpi(fd);
  if (!spi.ok() || spi.value() != self_token->spi) {
    LOG(WARNING) << "failed to verify negotiated rx psp spi";
    return nullptr;
  }

  DCHECK(socket->IsConnected());
  LOG(INFO) << "made psp " << *socket;
  return socket;
}

absl::StatusOr<PspToken> TcpPspManager::ExchangePspTokens(
    const TcpManager& tcp_mgr, const PspToken& peer_token,
    const Endpoint& self_target) {
  DCHECK(peer_token.IsValid());
  DCHECK(self_target.HasNonzeroIpPort());

  const Listener* const l = tcp_mgr.findListener(self_target);
  if (l == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no listener found for ", self_target.ToString()));
  }

  const fd_t fd = l->socket->fd();
  absl::MutexLock lock(*l->mu);
  absl::StatusOr<PspToken> self_token = psp::AddSecureListener(fd, peer_token);
  if (!self_token.ok() || !self_token->IsValid()) {
    return absl::InternalError("invalid self psp token");
  }
  return self_token;  // TODO(yyd): RemoveSecureListener when `fd` is closed?
}

}  // namespace peregrine::internal
