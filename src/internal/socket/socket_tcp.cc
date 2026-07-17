#include "src/internal/socket/socket_tcp.h"

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
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpSocket::Create(int family) {
  const fd_t fd = CreateSocket(family, SOCK_STREAM, /*blocking=*/true);
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
  ::close(fd_.value());
}

void TcpSocket::Shutdown() {
  LOG(INFO) << okMsg("shutdown");
  ::shutdown(fd_.value(), SHUT_RDWR);
  connected_ = false;
}

namespace {
int BindV4(fd_t fd, const Endpoint& local) {
  const struct sockaddr_in sa = local.BuildIPv4Sockaddr();
  return ::bind(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
}

int BindV6(fd_t fd, const Endpoint& local) {
  const struct sockaddr_in6 sa = local.BuildIPv6Sockaddr();
  return ::bind(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
}

int ConnectV4(fd_t fd, const Endpoint& peer) {
  const struct sockaddr_in sa = peer.BuildIPv4Sockaddr();
  return ::connect(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
}

int ConnectV6(fd_t fd, const Endpoint& peer) {
  const struct sockaddr_in6 sa = peer.BuildIPv6Sockaddr();
  return ::connect(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
}

int AcceptV4(fd_t fd) {
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  return ::accept4(fd.value(), (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
}

int AcceptV6(fd_t fd) {
  struct sockaddr_in6 sa;
  socklen_t len = sizeof(sa);
  return ::accept4(fd.value(), (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
}
}  // namespace

bool TcpSocket::Listen(const Endpoint& local) const {
  int on = 1;
  if ABSL_PREDICT_FALSE (!SetOption(fd_, SO_REUSEADDR, &on, sizeof(on))) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("set SO_REUSEADDR", last_errno);
    return false;
  }
  const auto bind = local.IsIPv4() ? BindV4 : BindV6;
  if ABSL_PREDICT_FALSE (bind(fd_, local) < 0) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("bind", last_errno);
    return false;
  } else if (ABSL_PREDICT_FALSE(::listen(fd_.value(), SOMAXCONN) < 0)) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("listen", last_errno);
    return false;
  } else {
    LOG(INFO) << okMsg("listening");
    return true;
  }
}

fd_t TcpSocket::Accept() const {
  DCHECK(IsBlocking());

  const auto accept = family_ == AF_INET ? AcceptV4 : AcceptV6;
  const int ret = accept(fd_);
  if ABSL_PREDICT_FALSE (ret < 0) {
    // At this point, shutdown() is the only reason that can cause EINVAL.
    if (const auto last_errno = errno; last_errno == EINVAL) {
      LOG(WARNING) << okMsg("accept shutdown");
      DCHECK(IsShutdown(-2));
      return fd_t(-2);
    } else {
      LOG(WARNING) << errMsg("accept", last_errno);
      return fd_t(-1);
    }
  } else {
    const fd_t new_fd(ret);
    DCHECK_GE(new_fd.value(), 0);
    LOG(INFO) << okMsg("accepted", new_fd);
    return new_fd;
  }
}

bool TcpSocket::Connect(const Endpoint& peer) {
  DCHECK(IsBlocking());

  const auto connect = peer.IsIPv4() ? ConnectV4 : ConnectV6;
  if ABSL_PREDICT_FALSE (connect(fd_, peer) < 0) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("connect", last_errno);
    return false;
  } else {
    LOG(INFO) << okMsg("connected");
    connected_ = true;
    return true;
  }
}

ssize_t TcpSocket::Send(const Byte* const buf, const size_t len) const {
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());
  DCHECK(IsBlocking());

  const Byte* ptr = buf;
  size_t sent = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::send(fd_.value(), ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      sent += bytes;
      DCHECK_EQ(buf + len, ptr + left);
      VLOG(1) << ioMsg("send", bytes);
    } else {
      const auto last_errno = errno;
      if ABSL_PREDICT_TRUE (bytes < 0) {
        if (Interrupted(last_errno)) continue;
        DCHECK(!WouldBlock(last_errno));
        LOG(WARNING) << errMsg("send", last_errno);
        return -1;
      } else {  // rarely happens
        DCHECK_EQ(bytes, 0);
        LOG(WARNING) << errMsg("send zero", last_errno);
        return 0;
      }
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(sent, len);
  return sent;
}

ssize_t TcpSocket::Recv(Byte* const buf, const size_t len) const {
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());
  DCHECK(IsBlocking());

  Byte* ptr = buf;
  size_t rcvd = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::recv(fd_.value(), ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      rcvd += bytes;
      DCHECK_EQ(buf + len, ptr + left);
      VLOG(1) << ioMsg("recv", bytes);
    } else if (bytes == 0) {  // peer closed connection
      LOG(INFO) << ioMsg("recv EoF", 0);
      return 0;
    } else {
      const auto last_errno = errno;
      if (Interrupted(last_errno)) continue;
      DCHECK(!WouldBlock(last_errno));
      LOG(WARNING) << errMsg("recv", last_errno);
      return -1;
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(rcvd, len);
  return rcvd;
}

/*static*/ absl::Status TcpSocket::Send(const fd_t fd, const Byte* const buf,
                                        const size_t len) {
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());
  DCHECK(IsBlockingMode(fd));

  const Byte* ptr = buf;
  size_t sent = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::send(fd.value(), ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      sent += bytes;
      DCHECK_EQ(buf + len, ptr + left);
    } else {
      if ABSL_PREDICT_TRUE (bytes < 0) {
        const auto last_errno = errno;
        if (Interrupted(last_errno)) continue;
        DCHECK(!WouldBlock(last_errno));
        return absl::InternalError("send");
      } else {  // rarely happens
        DCHECK_EQ(bytes, 0);
        return absl::InternalError("send zero");
      }
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(sent, len);
  return absl::OkStatus();
}

/*static*/ absl::Status TcpSocket::Recv(const fd_t fd, Byte* const buf,
                                        const size_t len) {
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());
  DCHECK(IsBlockingMode(fd));

  Byte* ptr = buf;
  size_t rcvd = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::recv(fd.value(), ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      rcvd += bytes;
      DCHECK_EQ(buf + len, ptr + left);
    } else if (bytes == 0) {  // peer closed connection
      return absl::InternalError("recv EoF");
    } else {
      const auto last_errno = errno;
      if (Interrupted(last_errno)) continue;
      DCHECK(!WouldBlock(last_errno));
      return absl::InternalError("recv");
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(rcvd, len);
  return absl::OkStatus();
}

std::string TcpSocket::ToString() const {
  return absl::StrCat("tcp socket: ", AddrPortPair(fd_));
}

}  // namespace peregrine::internal
