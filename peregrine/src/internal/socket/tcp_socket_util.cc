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

// Waits for the `event` on the `fd` by the `deadline`.
// Returns true if the event is received, false otherwise.
bool Wait(const fd_t fd, const int16_t event, const absl::Time deadline) {
  DCHECK(event == POLLIN || event == POLLOUT);
  struct pollfd pfd = {
      .fd = fd.value(),
      .events = event,
      .revents = 0,
  };
  while (true) {
    const absl::Duration remaining = deadline - absl::Now();
    if (remaining <= absl::ZeroDuration()) return false;
    const int64_t remaining_ms = absl::ToInt64Milliseconds(remaining);
    const int timeout_ms = static_cast<int>(
        std::min<int64_t>(remaining_ms, std::numeric_limits<int>::max()));
    const int ret = ::poll(&pfd, 1, timeout_ms);
    if ABSL_PREDICT_FALSE (ret < 0) {
      if (Interrupted(Errno(errno))) continue;
      return false;
    } else {
      if (ret == 0) return false;
      constexpr uint16_t kErrorEvents = POLLERR | POLLHUP | POLLNVAL;
      return (pfd.revents & (kErrorEvents | event)) == event;
    }
  }
}
}  // namespace

absl::Status TcpSocketUtil::Send(const fd_t fd,
                                 const absl::Span<const IoVec> iovecs,
                                 const absl::Time deadline) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK(IsValid(iovecs));
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  const bool has_deadline = deadline < absl::InfiniteFuture();
  const int flags = MSG_NOSIGNAL | (has_deadline ? MSG_DONTWAIT : 0);
  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t sent = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    if ABSL_PREDICT_FALSE (has_deadline && deadline < absl::Now()) {
      return absl::InternalError(
          ErrMsg("sendmsg timeout", fd, Errno(ETIMEDOUT)));
    }
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
        // When `has_deadline` is true, `MSG_DONTWAIT` is set so `sendmsg` can
        // return EAGAIN/EWOULDBLOCK on a blocking socket.
        if (has_deadline && WouldBlock(err)) {
          if (!Wait(fd, POLLOUT, deadline)) {
            return absl::InternalError(ErrMsg("sendmsg timeout", fd, err));
          }
          continue;
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
                                 const absl::Time deadline) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK(IsValid(iovecs));
  DCHECK_LE(iovecs.size(), IOV_MAX);

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);
  DCHECK_LE(len, std::numeric_limits<ssize_t>::max());

  const bool has_deadline = deadline < absl::InfiniteFuture();
  const int flags = has_deadline ? MSG_DONTWAIT : 0;
  std::vector<struct iovec> vecs{iovecs.begin(), iovecs.end()};
  const size_t n = vecs.size();
  size_t rcvd = 0;
  size_t i = 0;
  struct msghdr msg = {};
  while (i < n) {
    if ABSL_PREDICT_FALSE (has_deadline && deadline < absl::Now()) {
      return absl::InternalError(
          ErrMsg("recvmsg timeout", fd, Errno(ETIMEDOUT)));
    }
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
      // When `has_deadline` is true, `MSG_DONTWAIT` is set so `recvmsg` can
      // return EAGAIN/EWOULDBLOCK on a blocking socket.
      if (has_deadline && WouldBlock(err)) {
        if (!Wait(fd, POLLIN, deadline)) {
          return absl::InternalError(ErrMsg("recvmsg timeout", fd, err));
        }
        continue;
      }
      return absl::InternalError(ErrMsg("recvmsg", fd, err));
    }
  }
  DCHECK_EQ(rcvd, len);
  return absl::OkStatus();
}

}  // namespace peregrine::internal
