#include "peregrine/src/internal/socket/socket_udp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
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
    return absl::WrapUnique(new UdpSocket(fd, family));
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
  DCHECK(IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const size_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  DCHECK_LE(all, std::numeric_limits<ssize_t>::max());

  while (true) {
    const ssize_t bytes =
        ::writev(fd_.value(), iovs.Head(), iovs.RemainingItems());
    DCHECK(bytes == all || bytes < 0);
    if ABSL_PREDICT_TRUE (bytes == all) {
      VLOG(1) << ioMsg("writev", bytes);
      iovs.Advance(bytes);
      return bytes;
    }
    const Errno err(errno);
    if (Interrupted(err)) continue;
    DCHECK(!WouldBlock(err));
    LOG(WARNING) << errMsg("writev", err);
    return -1;
  }
}

ssize_t UdpSocket::Recv(IoVecCursor& iovs) const {
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_LE(iovs.TotalItems(), IOV_MAX);

  const size_t all = iovs.RemainingBytes();
  DCHECK_GE(all, 1);
  DCHECK_LE(all, std::numeric_limits<ssize_t>::max());

  while (true) {
    const ssize_t bytes =
        ::readv(fd_.value(), iovs.Head(), iovs.RemainingItems());
    if ABSL_PREDICT_TRUE (bytes > 0) {
      VLOG(1) << ioMsg("readv", bytes);
      iovs.Advance(bytes);
      return bytes;
    } else if (bytes < 0) {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      DCHECK(!WouldBlock(err));
      LOG(WARNING) << errMsg("readv", err);
      return -1;
    } else {
      DCHECK_EQ(bytes, 0);
      LOG(INFO) << ioMsg("readv no payload", 0);
      return 0;
    }
  }
}

std::string UdpSocket::ToString() const {
  return absl::StrCat("udp/", SocketBase::ToString());
}

}  // namespace peregrine::internal
