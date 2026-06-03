#include "src/internal/socket/acceptor.h"

#include <atomic>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kAcceptor = "tcp acceptor ";
}  // namespace

std::unique_ptr<TcpAcceptor> TcpAcceptor::Create(const Endpoint& local) {
  DCHECK(local.IsValid());

  const int family = local.GetIpAddr().AddressFamily();
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  DCHECK(socket->IsValid());
  DCHECK(socket->IsBlocking());
  if ABSL_PREDICT_FALSE (!socket->Listen(local)) {
    return nullptr;
  }

  LOG(INFO) << kAcceptor << "created, " << *socket;
  return absl::WrapUnique(new TcpAcceptor(std::move(socket)));
}

void TcpAcceptor::Start(AcceptCallback accept) {
  DCHECK_NE(accept, nullptr);
  LOG(INFO) << kAcceptor << "starting, " << *listener_;

  const int family = listener_->family();
  while (!stop_.load(std::memory_order_relaxed)) {
    DCHECK(listener_->IsBlocking());
    const int fd = listener_->Accept();
    if ABSL_PREDICT_FALSE (fd < 0) {
      // TODO(yongx): Handle errors.
      continue;
    }
    std::unique_ptr<TcpSocket> socket = TcpSocket::Create(fd, family);
    DCHECK(socket->IsValid());
    DCHECK(socket->IsConnected());
    DCHECK(socket->IsBlocking());
    LOG(INFO) << kAcceptor << "accepted, " << *socket;
    accept(std::move(socket));
  }
}

void TcpAcceptor::Stop() {
  stop_.store(true, std::memory_order_relaxed);
  DCHECK(invariant());
  // Shuts down the listening socket to unblock its Accept() call.
  ::shutdown(listener_->fd(), SHUT_RDWR);
  LOG(INFO) << kAcceptor << "stopped, " << *listener_;
}

}  // namespace peregrine::internal
