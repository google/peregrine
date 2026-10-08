#include "peregrine/src/internal/socket/tcp_socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/internal/util/util.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

namespace {
using util::Errno;

inline std::string ErrMsg(std::string_view what, fd_t fd, Errno err) {
  return absl::StrFormat("tcp socket %s failed: fd=%d %s errno=%d (%s)", what,
                         fd.value(), AddrPortPair(fd), err.value(),
                         std::strerror(err.value()));
}

inline absl::Time GetDeadline(int timeout_ms) {
  return timeout_ms >= 0 ? absl::Now() + absl::Milliseconds(timeout_ms)
                         : absl::InfiniteFuture();
}

// Waits for the `event` on the `fd` by the `deadline`.
// Returns true if the `fd` is ready for the `event` or has an error or hangup,
// in which case the next send/recv call reports the actual result.
// Returns false on timeout or `poll` failure.
bool Wait(const fd_t fd, const int16_t event, const absl::Time deadline) {
  DCHECK(event == POLLIN || event == POLLOUT);
  DCHECK_LT(deadline, absl::InfiniteFuture());
  struct pollfd pfd = {
      .fd = fd.value(),
      .events = event,
      .revents = 0,
  };
  while (true) {
    const int64_t ms = absl::ToInt64Milliseconds(deadline - absl::Now());
    if (ms <= 0) return false;

    constexpr int kMax = std::numeric_limits<int>::max();
    const int timeout_ms = static_cast<int>(std::min<int64_t>(ms, kMax));
    const int ret = ::poll(&pfd, /*nfds=*/1, timeout_ms);
    if (ret < 0 && Interrupted(Errno(errno))) continue;
    return ret > 0;
  }
}
}  // namespace

absl::Status TcpSocketUtil::Send(const fd_t fd,
                                 const absl::Span<const IoVec> iovecs,
                                 const int timeout_ms) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK(IsValid(iovecs));
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  const absl::Time deadline = GetDeadline(timeout_ms);
  const int flags = (timeout_ms >= 0 ? MSG_DONTWAIT : 0) | MSG_NOSIGNAL;
  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t sent = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    msg.msg_iov = &vecs[i];
    msg.msg_iovlen = n - i;
    const ssize_t bytes = ::sendmsg(fd.value(), &msg, flags);
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
      DCHECK_LT(bytes, 0);
      if ABSL_PREDICT_TRUE (bytes < 0) {
        const Errno err(errno);
        if (Interrupted(err)) continue;
        if (WouldBlock(err)) {
          DCHECK(flags & MSG_DONTWAIT);
          if (Wait(fd, POLLOUT, deadline)) continue;
          return absl::InternalError(ErrMsg("sendmsg timeout", fd, err));
        }
        return absl::InternalError(ErrMsg("sendmsg", fd, err));
      }
    }
  }
  DCHECK_EQ(sent, len);
  return absl::OkStatus();
}

absl::Status TcpSocketUtil::Recv(const fd_t fd,
                                 const absl::Span<const IoVec> iovecs,
                                 const int timeout_ms) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK(IsValid(iovecs));
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  const absl::Time deadline = GetDeadline(timeout_ms);
  const int flags = timeout_ms >= 0 ? MSG_DONTWAIT : 0;
  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t rcvd = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    msg.msg_iov = &vecs[i];
    msg.msg_iovlen = n - i;
    const ssize_t bytes = ::recvmsg(fd.value(), &msg, flags);
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
      return absl::InternalError("recvmsg eof");
    } else {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) {
        DCHECK(flags & MSG_DONTWAIT);
        if (Wait(fd, POLLIN, deadline)) continue;
        return absl::InternalError(ErrMsg("recvmsg timeout", fd, err));
      }
      return absl::InternalError(ErrMsg("recvmsg", fd, err));
    }
  }
  DCHECK_EQ(rcvd, len);
  return absl::OkStatus();
}

}  // namespace peregrine::internal
