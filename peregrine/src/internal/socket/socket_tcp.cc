#include "peregrine/src/internal/socket/socket_tcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/internal/base/constants.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/socket_base.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

namespace {
using util::Errno;
}  // namespace

std::unique_ptr<TcpSocket> TcpSocket::Create(int family, bool blocking) {
  const fd_t fd = CreateSocket(family, SOCK_STREAM, blocking);
  if ABSL_PREDICT_FALSE (fd.value() < 0) {
    return nullptr;
  } else {
    LOG(INFO) << okMsg("created", fd);
    return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/false));
  }
}

std::unique_ptr<TcpSocket> TcpSocket::Create(fd_t fd, int family) {
  return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/true));
}

TcpSocket::~TcpSocket() {
  DCHECK(invariant());
  if (connected_) Shutdown();
  DCHECK(!connected_);
  LOG(INFO) << okMsg("closing");
  DCHECK(invariant());
  ::close(fd_.value());
  fd_ = fd_t(-1);
}

void TcpSocket::Shutdown() {
  DCHECK(invariant());
  LOG(INFO) << okMsg("shutdown");
  ::shutdown(fd_.value(), SHUT_RDWR);
  connected_ = false;
  DCHECK(invariant());
}

int TcpSocket::Bind(const Endpoint& local) const {
  DCHECK(invariant());
  if ABSL_PREDICT_FALSE (SocketBase::Bind(fd_, local) < 0) {
    const Errno err(errno);
    LOG(WARNING) << errMsg("bind", err);
    return -1;
  } else {
    LOG(INFO) << okMsg("bound");
    return 0;
  }
}

int TcpSocket::Listen(const Endpoint& local) const {
  DCHECK(invariant());

  int on = 1;
  if ABSL_PREDICT_FALSE (SetSocketOption(fd_, SO_REUSEADDR, &on, sizeof(on))) {
    const Errno err(errno);
    LOG(WARNING) << errMsg("set SO_REUSEADDR", err);
    return -1;
  }
  if ABSL_PREDICT_FALSE (SocketBase::Bind(fd_, local) < 0) {
    const Errno err(errno);
    LOG(WARNING) << errMsg("bind", err);
    return -1;
  } else if (ABSL_PREDICT_FALSE(::listen(fd_.value(), kTcpListenBacklog) < 0)) {
    const Errno err(errno);
    LOG(WARNING) << errMsg("listen", err);
    return -1;
  } else {
    LOG(INFO) << okMsg("listening");
    return 0;
  }
}

int TcpSocket::Accept(bool gen_blocking) const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());

  const int flags = (gen_blocking ? 0 : SOCK_NONBLOCK) | SOCK_CLOEXEC;
  while (true) {
    const int ret = ::accept4(fd_.value(), nullptr, nullptr, flags);
    if (ret < 0) {
      const Errno err(errno);
      if (WouldBlock(err)) {
        return kAcceptWouldBlock;
      } else if (Interrupted(err) || err.value() == EPROTO ||
                 err.value() == ECONNABORTED) {
        continue;
      } else if (err.value() == EINVAL) {  // after shutdown()
        LOG(INFO) << okMsg("accept shutdown");
        return kAcceptShutdown;
      } else if (OutOfResource(err)) {
        return kAcceptOutOfResource;
      } else {
        LOG(WARNING) << errMsg("accept", err);
        return kAcceptError;
      }
    } else {
      const fd_t new_fd(ret);
      DCHECK(MatchesBlockingMode(new_fd, gen_blocking));
      LOG(INFO) << okMsg("accepted", new_fd);
      return ret;
    }
  }
}

int TcpSocket::Connect(const Endpoint& peer) {
  DCHECK(invariant());
  DCHECK(!connected_);
  DCHECK(IsNonBlocking() || IsBlocking());

  // Treat EINTR as an error: do not reconnect the same socket.
  if (SocketBase::Connect(fd_, peer) < 0) {
    const Errno err(errno);
    if (InProgress(err)) return kConnectInProgress;
    LOG(WARNING) << errMsg("connect", err);
    return kConnectError;
  } else {
    LOG(INFO) << okMsg("connected");
    connected_ = true;
    return kConnectSuccess;
  }
}

ssize_t TcpSocket::Send(IoVecCursor& iovs) const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const ssize_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  ssize_t sent = 0;
  struct msghdr msg = {};
  while (true) {
    DCHECK_GE(iovs.RemainingBytes(), 1);
    msg.msg_iov = iovs.Head();
    msg.msg_iovlen = iovs.RemainingItems();
    const ssize_t bytes = ::sendmsg(fd_.value(), &msg, MSG_NOSIGNAL);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      sent += bytes;
      if (iovs.Advance(bytes)) break;
    } else {
      DCHECK_LT(bytes, 0);
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) break;
      LOG(WARNING) << errMsg("sendmsg", err);
      return -1;
    }
  }
  DCHECK((IsBlocking() && sent == all) ||
         (IsNonBlocking() && 0 <= sent && sent <= all));
  return sent;
}

ssize_t TcpSocket::Recv(IoVecCursor& iovs) const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const ssize_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  ssize_t rcvd = 0;
  while (true) {
    DCHECK_GE(iovs.RemainingBytes(), 1);
    const ssize_t bytes =
        ::readv(fd_.value(), iovs.Head(), iovs.RemainingItems());
    if ABSL_PREDICT_TRUE (bytes > 0) {
      rcvd += bytes;
      if (iovs.Advance(bytes)) break;
    } else if (bytes == 0) {  // peer closed connection
      LOG(INFO) << ioMsg("readv eof", 0);
      return rcvd;
    } else {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) break;
      LOG(WARNING) << errMsg("readv", err);
      return -1;
    }
  }
  DCHECK((IsBlocking() && rcvd == all) ||
         (IsNonBlocking() && 0 <= rcvd && rcvd <= all));
  return rcvd;
}

std::string TcpSocket::ToString() const {
  return absl::StrCat("tcp/", SocketBase::ToString());
}

}  // namespace peregrine::internal
