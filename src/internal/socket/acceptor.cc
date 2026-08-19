#include "src/internal/socket/acceptor.h"

#include <atomic>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<TcpAcceptor> TcpAcceptor::Create(const Endpoint& local) {
  const int family = local.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  DCHECK(socket->IsBlocking());
  if ABSL_PREDICT_FALSE (!socket->Listen(local)) {
    return nullptr;
  }

  LOG(INFO) << "created, " << *socket;
  return absl::WrapUnique(new TcpAcceptor(std::move(socket)));
}

void TcpAcceptor::Start(AcceptCallback accept) {
  DCHECK(invariant());
  DCHECK_NE(accept, nullptr);
  LOG(INFO) << "starting, " << *listener_;

  const int family = listener_->family();
  while (!stop_.load(std::memory_order_relaxed)) {
    DCHECK(listener_->IsBlocking());
    const fd_t fd = listener_->Accept();
    if ABSL_PREDICT_FALSE (fd.value() < 0) {
      if (IsShutdown(fd.value())) return;
      // TODO(yongx): Handle errors.
      DCHECK_EQ(fd.value(), -1);
      continue;
    }
    std::unique_ptr<TcpSocket> socket = TcpSocket::Create(fd, family);
    DCHECK(socket->IsBlocking());
    DCHECK(socket->IsConnected());
    LOG(INFO) << "made " << *socket;
    accept(std::move(socket));
  }
}

void TcpAcceptor::Stop() {
  DCHECK(invariant());
  stop_.store(true, std::memory_order_relaxed);
  listener_->Shutdown();  // unblocks Accept()
  LOG(INFO) << "stopped, " << *listener_;
}

}  // namespace peregrine::internal
