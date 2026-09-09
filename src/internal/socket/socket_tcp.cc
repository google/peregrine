#include "src/internal/socket/socket_tcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "third_party/liburing/src/include/liburing.h"
#include "third_party/liburing/src/include/liburing/io_uring.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_base.h"
#include "src/internal/socket/socket_util.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

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

std::unique_ptr<struct io_uring> TcpSocket::uringInit() {
  const uint32_t kEntries = 128;
  const uint32_t kFlags = 0;
  auto ring = std::make_unique<struct io_uring>();
  if (int ret = io_uring_queue_init(kEntries, ring.get(), kFlags); ret != 0) {
    LOG(WARNING) << "io_uring_queue_init failed: " << std::strerror(-ret);
    return nullptr;
  }
  return ring;
}

void TcpSocket::uringShutdown() {
  if (ring_ == nullptr) return;
  io_uring_queue_exit(ring_.get());
  ring_ = nullptr;
}

TcpSocket::~TcpSocket() {
  DCHECK(invariant());
  uringShutdown();
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

bool TcpSocket::Bind(const Endpoint& local) const {
  DCHECK(invariant());
  if ABSL_PREDICT_FALSE (SocketBase::Bind(fd_, local) < 0) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("bind", last_errno);
    return false;
  } else {
    LOG(INFO) << okMsg("bound");
    return true;
  }
}

bool TcpSocket::Listen(const Endpoint& local) const {
  DCHECK(invariant());

  int on = 1;
  if ABSL_PREDICT_FALSE (!SetOption(fd_, SO_REUSEADDR, &on, sizeof(on))) {
    const auto last_errno = errno;
    LOG(WARNING) << errMsg("set SO_REUSEADDR", last_errno);
    return false;
  }
  if ABSL_PREDICT_FALSE (SocketBase::Bind(fd_, local) < 0) {
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

namespace {
int AcceptConn(const int family, const fd_t fd) {
  if (family == AF_INET) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    return ::accept4(fd.value(), (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
  } else {
    DCHECK_EQ(family, AF_INET6);
    struct sockaddr_in6 sa;
    socklen_t len = sizeof(sa);
    return ::accept4(fd.value(), (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
  }
}
}  // namespace

fd_t TcpSocket::Accept() const {
  DCHECK(invariant());
  DCHECK(IsNonBlocking() || IsBlocking());

  const int ret = AcceptConn(family_, fd_);
  if ABSL_PREDICT_FALSE (ret < 0) {
    // At this point, shutdown() is the only reason that can cause EINVAL.
    const auto last_errno = errno;
    if (WouldBlock(last_errno)) {
      return fd_t(-3);
    } else if (last_errno == EINVAL) {
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
  DCHECK(invariant());
  DCHECK(IsBlocking());

  if ABSL_PREDICT_FALSE (SocketBase::Connect(fd_, peer) < 0) {
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
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  const Byte* ptr = buf;
  size_t sent = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::send(fd_.value(), ptr, left, MSG_NOSIGNAL);
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

ssize_t TcpSocket::SendV(const absl::Span<const IoVec> iovecs) const {
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t sent = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    msg.msg_iov = &vecs[i];
    msg.msg_iovlen = n - i;
    const ssize_t bytes = ::sendmsg(fd_.value(), &msg, MSG_NOSIGNAL);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      sent += bytes;
      if ABSL_PREDICT_TRUE (sent >= len) break;
      size_t b = static_cast<size_t>(bytes);
      while (i < n && vecs[i].iov_len <= b) {  // advance iov index
        b -= vecs[i].iov_len;
        ++i;
      }
      if (i >= n) break;
      if (b > 0) {  // adjust iov ptr/len
        vecs[i].iov_base = static_cast<Byte*>(vecs[i].iov_base) + b;
        vecs[i].iov_len -= b;
      }
    } else {
      const auto last_errno = errno;
      if ABSL_PREDICT_TRUE (bytes < 0) {
        if (Interrupted(last_errno)) continue;
        DCHECK(!WouldBlock(last_errno));
        LOG(WARNING) << errMsg("sendmsg", last_errno);
        return -1;
      } else {  // rarely happens
        DCHECK_EQ(bytes, 0);
        LOG(WARNING) << errMsg("sendmsg zero", last_errno);
        return 0;
      }
    }
  }
  DCHECK_EQ(sent, len);
  return sent;
}

ssize_t TcpSocket::Recv(Byte* const buf, const size_t len) const {
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

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
      LOG(INFO) << ioMsg("recv eof", 0);
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

ssize_t TcpSocket::RecvV(const absl::Span<const IoVec> iovecs) const {
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const int n = vecs.size();
  size_t rcvd = 0;
  int i = 0;
  while (i < n) {
    const ssize_t bytes = ::readv(fd_.value(), &vecs[i], n - i);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      rcvd += bytes;
      if ABSL_PREDICT_TRUE (rcvd >= len) break;
      size_t b = static_cast<size_t>(bytes);
      while (i < n && vecs[i].iov_len <= b) {  // advance iov index
        b -= vecs[i].iov_len;
        ++i;
      }
      if (i >= n) break;
      if (b > 0) {  // adjust iov ptr/len
        vecs[i].iov_base = static_cast<Byte*>(vecs[i].iov_base) + b;
        vecs[i].iov_len -= b;
      }
    } else if (bytes == 0) {  // peer closed connection
      LOG(INFO) << ioMsg("readv eof", 0);
      return 0;
    } else {
      const auto last_errno = errno;
      if (Interrupted(last_errno)) continue;
      DCHECK(!WouldBlock(last_errno));
      LOG(WARNING) << errMsg("readv", last_errno);
      return -1;
    }
  }
  DCHECK_EQ(rcvd, len);
  return rcvd;
}

struct io_uring_sqe* TcpSocket::uringGetSqe() {
  DCHECK_NE(ring_, nullptr);

  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_.get());
  if ABSL_PREDICT_FALSE (sqe == nullptr) {
    int ret = 0;
    while (true) {
      ret = io_uring_submit(ring_.get());
      if (ret == -EINTR) continue;
      break;
    }
    if (ret < 0) {
      LOG(WARNING) << "tcp io_uring_submit failed: " << std::strerror(-ret);
      return nullptr;
    }
    sqe = io_uring_get_sqe(ring_.get());
    if (sqe == nullptr) {
      LOG(WARNING) << "tcp io_uring SQ is full";
      return nullptr;
    }
  }
  return sqe;
}

int TcpSocket::uringSubmitAndWait() {
  DCHECK_NE(ring_, nullptr);

  int ret = 0;
  while (true) {
    ret = io_uring_submit_and_wait(ring_.get(), /*wait_nr=*/1);
    if (ret == -EINTR) continue;
    break;
  }
  if ABSL_PREDICT_FALSE (ret < 0) {
    LOG(WARNING) << "tcp io_uring_submit_and_wait failed: "
                 << std::strerror(-ret);
    return ret;
  }

  struct io_uring_cqe* cqe = nullptr;
  while (true) {
    ret = io_uring_wait_cqe(ring_.get(), &cqe);
    if (ret == -EINTR) continue;
    break;
  }
  if ABSL_PREDICT_FALSE (ret < 0) {
    LOG(WARNING) << "tcp io_uring_wait_cqe failed: " << std::strerror(-ret);
    return ret;
  }

  const int res = cqe->res;
  io_uring_cqe_seen(ring_.get(), cqe);
  return res;
}

ssize_t TcpSocket::SendUring(const Byte* buf, size_t len) {
  DCHECK_NE(ring_, nullptr);
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<int32_t>::max());

  const Byte* ptr = buf;
  size_t sent = 0;
  ssize_t left = len;
  while (left > 0) {
    struct io_uring_sqe* sqe = uringGetSqe();
    if ABSL_PREDICT_FALSE (sqe == nullptr) return -1;
    io_uring_prep_send(sqe, fd_.value(), ptr, left, MSG_NOSIGNAL);

    const int res = uringSubmitAndWait();
    if ABSL_PREDICT_TRUE (res > 0) {
      DCHECK_LE(res, left);
      ptr += res;
      left -= res;
      sent += res;
      DCHECK_EQ(buf + len, ptr + left);
      VLOG(1) << ioMsg("send", res);
    } else {
      const int last_errno = -res;
      if (res < 0) {
        if (Interrupted(last_errno)) continue;
        DCHECK(!WouldBlock(last_errno));
        LOG(WARNING) << errMsg("send", last_errno);
        return -1;
      } else {
        DCHECK_EQ(res, 0);
        LOG(WARNING) << errMsg("send zero", last_errno);
        return 0;
      }
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(sent, len);
  return sent;
}

ssize_t TcpSocket::SendVUring(absl::Span<const IoVec> iovecs) {
  DCHECK_NE(ring_, nullptr);
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<int32_t>::max());

  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t sent = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    struct io_uring_sqe* sqe = uringGetSqe();
    if ABSL_PREDICT_FALSE (sqe == nullptr) return -1;
    msg.msg_iov = &vecs[i];
    msg.msg_iovlen = n - i;
    io_uring_prep_sendmsg(sqe, fd_.value(), &msg, MSG_NOSIGNAL);

    const int res = uringSubmitAndWait();
    if ABSL_PREDICT_TRUE (res > 0) {
      sent += res;
      VLOG(1) << ioMsg("sendv", res);
      if ABSL_PREDICT_TRUE (sent >= len) break;
      size_t b = static_cast<size_t>(res);
      while (i < n && vecs[i].iov_len <= b) {
        b -= vecs[i].iov_len;
        ++i;
      }
      if (i >= n) break;
      if (b > 0) {
        vecs[i].iov_base = static_cast<Byte*>(vecs[i].iov_base) + b;
        vecs[i].iov_len -= b;
      }
    } else {
      const int last_errno = -res;
      if (res < 0) {
        if (Interrupted(last_errno)) continue;
        DCHECK(!WouldBlock(last_errno));
        LOG(WARNING) << errMsg("sendv", last_errno);
        return -1;
      } else {
        DCHECK_EQ(res, 0);
        LOG(WARNING) << errMsg("sendv zero", last_errno);
        return 0;
      }
    }
  }
  DCHECK_EQ(sent, len);
  return sent;
}

ssize_t TcpSocket::RecvUring(Byte* buf, size_t len) {
  DCHECK_NE(ring_, nullptr);
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<int32_t>::max());

  Byte* ptr = buf;
  size_t rcvd = 0;
  ssize_t left = len;
  while (left > 0) {
    struct io_uring_sqe* sqe = uringGetSqe();
    if ABSL_PREDICT_FALSE (sqe == nullptr) return -1;
    io_uring_prep_recv(sqe, fd_.value(), ptr, left, /*flags=*/0);

    const int res = uringSubmitAndWait();
    if ABSL_PREDICT_TRUE (res > 0) {
      DCHECK_LE(res, left);
      ptr += res;
      left -= res;
      rcvd += res;
      DCHECK_EQ(buf + len, ptr + left);
      VLOG(1) << ioMsg("recv", res);
    } else if (res == 0) {
      LOG(INFO) << ioMsg("recv eof", 0);
      return 0;
    } else {
      const int last_errno = -res;
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

ssize_t TcpSocket::RecvVUring(absl::Span<const IoVec> iovecs) {
  DCHECK_NE(ring_, nullptr);
  DCHECK(invariant());
  DCHECK(IsBlocking());
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<int32_t>::max());

  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t rcvd = 0;
  size_t i = 0;
  while (i < n) {
    struct io_uring_sqe* sqe = uringGetSqe();
    if ABSL_PREDICT_FALSE (sqe == nullptr) return -1;
    io_uring_prep_readv(sqe, fd_.value(), &vecs[i], n - i, /*offset=*/0);

    const int res = uringSubmitAndWait();
    if ABSL_PREDICT_TRUE (res > 0) {
      rcvd += res;
      VLOG(1) << ioMsg("recvv", res);
      if ABSL_PREDICT_TRUE (rcvd >= len) break;
      size_t b = static_cast<size_t>(res);
      while (i < n && vecs[i].iov_len <= b) {
        b -= vecs[i].iov_len;
        ++i;
      }
      if (i >= n) break;
      if (b > 0) {
        vecs[i].iov_base = static_cast<Byte*>(vecs[i].iov_base) + b;
        vecs[i].iov_len -= b;
      }
    } else if (res == 0) {
      LOG(INFO) << ioMsg("recvv eof", 0);
      return 0;
    } else {
      const int last_errno = -res;
      if (Interrupted(last_errno)) continue;
      DCHECK(!WouldBlock(last_errno));
      LOG(WARNING) << errMsg("recvv", last_errno);
      return -1;
    }
  }
  DCHECK_EQ(rcvd, len);
  return rcvd;
}

std::string TcpSocket::ToString() const {
  return absl::StrFormat("tcp socket: %s, io_uring=%s", AddrPortPair(fd_),
                         ring_ != nullptr ? "enabled" : "disabled");
}

}  // namespace peregrine::internal
