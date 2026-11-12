#include "peregrine/src/internal/socket/socket_udp.h"

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

std::unique_ptr<UdpSocket> UdpSocket::Create(int family, bool blocking) {
  const fd_t fd = CreateSocket(family, SOCK_DGRAM, blocking);
  if ABSL_PREDICT_FALSE (fd.value() < 0) {
    return nullptr;
  } else {
    LOG(INFO) << okMsg("created", fd);
    return absl::WrapUnique(new UdpSocket(fd, family, blocking));
  }
}

UdpSocket::~UdpSocket() {
  DCHECK(invariant());
  if (connected_) Shutdown();
  DCHECK(!connected_);
  LOG(INFO) << okMsg("closing");
  DCHECK(invariant());
  ::close(fd_.value());
  fd_ = fd_t(-1);
}

void UdpSocket::Shutdown() {
  DCHECK(invariant());
  LOG(INFO) << okMsg("shutdown");
  ::shutdown(fd_.value(), SHUT_RDWR);
  connected_ = false;
  DCHECK(invariant());
}

int UdpSocket::Bind(const Endpoint& local) const {
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

int UdpSocket::Connect(const Endpoint& peer) {
  DCHECK(invariant());
  if ABSL_PREDICT_FALSE (SocketBase::Connect(fd_, peer) < 0) {
    const Errno err(errno);
    LOG(WARNING) << errMsg("connect", err);
    return -1;
  } else {
    LOG(INFO) << okMsg("connected");
    connected_ = true;
    return 0;
  }
}

ssize_t UdpSocket::Send(IoVecCursor& iovs) const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const ssize_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  struct msghdr msg = {};
  while (true) {
    DCHECK_GE(iovs.RemainingBytes(), 1);
    msg.msg_iov = iovs.Head();
    msg.msg_iovlen = iovs.RemainingItems();
    const ssize_t bytes = ::sendmsg(fd_.value(), &msg, MSG_NOSIGNAL);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_EQ(bytes, all);
      VLOG(1) << ioMsg("sendmsg", bytes);
      iovs.Advance(bytes);
      return bytes;
    } else {
      DCHECK_LT(bytes, 0);
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) return 0;
      LOG(WARNING) << errMsg("sendmsg", err);
      return -1;
    }
  }
}

ssize_t UdpSocket::Recv(IoVecCursor& iovs) const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const ssize_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  struct msghdr msg = {};
  while (true) {
    DCHECK_GE(iovs.RemainingBytes(), 1);
    msg.msg_iov = iovs.Head();
    msg.msg_iovlen = iovs.RemainingItems();
    const ssize_t bytes = ::recvmsg(fd_.value(), &msg, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, all);
      VLOG(1) << ioMsg("recvmsg", bytes);
      iovs.Advance(bytes);
      return bytes;
    } else if (bytes < 0) {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) return 0;
      LOG(WARNING) << errMsg("recvmsg", err);
      return -1;
    } else {
      DCHECK_EQ(bytes, 0);
      LOG(INFO) << ioMsg("recvmsg no payload", 0);
      return 0;
    }
  }
}

std::string UdpSocket::ToString() const {
  return absl::StrCat("udp/", SocketBase::ToString());
}

}  // namespace peregrine::internal
