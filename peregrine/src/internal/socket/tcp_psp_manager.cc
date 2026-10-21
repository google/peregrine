#include "peregrine/src/internal/socket/tcp_psp_manager.h"

#include <poll.h>
#include <sys/epoll.h>

#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/psp/psp_util.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal {

absl::StatusOr<PspToken> PspTcpManager::ExchangePspTokens(
    const TcpManager& tcp_mgr, const PspToken& peer_token,
    const Endpoint& self_target) {
  DCHECK(peer_token.IsValid());
  DCHECK(self_target.HasNonzeroIpPort());

  const auto* const l = tcp_mgr.FindListener(self_target);
  if (l == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no listener found for ", self_target.ToString()));
  }

  const fd_t fd = l->socket->fd();
  static absl::Mutex mu;  // TODO(yyd): use a per-listener mutex if needed.
  absl::MutexLock lock(mu);
  absl::StatusOr<PspToken> self_token = psp::AddSecureListener(fd, peer_token);
  if (!self_token.ok() || !self_token->IsValid()) {
    return absl::InternalError("invalid self psp token");
  }
  // RemoveSecureListener(fd) is automatically called when `fd` is closed.
  return self_token;
}

namespace {
absl::StatusOr<PspToken> ConfigPspToken(
    const fd_t fd, const Endpoint& peer, const Endpoint& peer_control,
    PspTcpManager::PspTokenExchange& psp_token_xchg) {
  const absl::StatusOr<PspToken> self_token = psp::AcquireRxSpiAndKey(fd);
  if (!self_token.ok() || !self_token->IsValid()) {
    return absl::InternalError("failed to acquire self rx psp token");
  }
  const absl::StatusOr<PspToken> peer_token =
      psp_token_xchg(self_token.value(), peer, peer_control);  // slow!
  if (!peer_token.ok()) {
    return absl::InternalError("failed to exchange psp token with peer");
  }
  const absl::Status status = psp::SetTxSpiAndKey(fd, peer_token.value());
  if (!status.ok()) {
    return absl::InternalError("failed to set tx psp token");
  }
  return self_token.value();
}

bool CheckPspToken(const fd_t fd, const PspToken& self_token) {
  const absl::StatusOr<Spi> spi = psp::GetInitialRxSpi(fd);
  if (!spi.ok() || spi.value() != self_token.spi) {
    LOG(WARNING) << "failed to verify negotiated rx psp spi";
    return false;
  }
  return true;
}
}  // namespace

std::unique_ptr<TcpSocket> PspTcpManager::ConnectPsp(
    const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
    PspTokenExchange& psp_token_xchg, const int timeout_ms) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(peer_control.HasNonzeroIpPort());
  DCHECK_NE(psp_token_xchg, nullptr);

  const int family = peer.GetIpAddr().AddressFamily();
  const bool blocking = timeout_ms <= 0;
  auto socket = TcpSocket::Create(family, blocking);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }
  if (!self.HasZeroIpAddr() && socket->Bind(self)) {
    return nullptr;
  }

  const fd_t fd = socket->fd();
  const absl::StatusOr<PspToken> self_token =
      ConfigPspToken(fd, peer, peer_control, psp_token_xchg);
  if (!self_token.ok() || !self_token->IsValid()) return nullptr;

  socket = TcpManager::Connect(std::move(socket), peer, timeout_ms);
  if (socket == nullptr) return nullptr;

  if (!CheckPspToken(fd, self_token.value())) return nullptr;

  DCHECK(socket->IsConnected());
  LOG(INFO) << "made psp " << *socket;
  return socket;
}

}  // namespace peregrine::internal
