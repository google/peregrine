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

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/clock.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

namespace {
using util::Errno;

inline std::string ErrMsg(std::string_view what, fd_t fd) {
  return absl::StrFormat("tcp socket %s failed: fd=%d %s", what, fd.value(),
                         AddrPortPair(fd));
}

inline std::string ErrMsg(std::string_view what, fd_t fd, Errno err) {
  const std::string s = ErrMsg(what, fd);
  return absl::ErrnoToStatus(err.value(), s).ToString();
}

inline absl::Duration GetDeadline(int timeout_ms) {
  return timeout_ms >= 0 ? util::MonotonicNow() + absl::Milliseconds(timeout_ms)
                         : absl::InfiniteDuration();
}

// Waits for the `event` on the `fd` by the `deadline` on the monotonic clock.
// Returns true if the `fd` is ready for the `event` or has an error or hangup,
// in which case the next send/recv call reports the actual result.
// Returns false on timeout or `poll` failure.
bool Wait(const fd_t fd, const int16_t event, const absl::Duration deadline) {
  DCHECK(event == POLLIN || event == POLLOUT);
  DCHECK_LT(deadline, absl::InfiniteDuration());
  struct pollfd pfd = {
      .fd = fd.value(),
      .events = event,
      .revents = 0,
  };
  while (true) {
    const absl::Duration now = util::MonotonicNow();
    const int64_t ms = absl::ToInt64Milliseconds(deadline - now);
    if (ms <= 0) return false;

    constexpr int kMax = std::numeric_limits<int>::max();
    const int timeout_ms = static_cast<int>(std::min<int64_t>(ms, kMax));
    const int ret = ::poll(&pfd, /*nfds=*/1, timeout_ms);
    if (ret < 0 && Interrupted(Errno(errno))) continue;
    return ret > 0;
  }
}
}  // namespace

absl::Status TcpSocketUtil::Send(const fd_t fd, IoVecCursor& iovecs,
                                 const int timeout_ms) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK_LE(iovecs.TotalItems(), IOV_MAX);

  const absl::Duration deadline = GetDeadline(timeout_ms);
  const int flags = (timeout_ms >= 0 ? MSG_DONTWAIT : 0) | MSG_NOSIGNAL;
  const ssize_t all = iovecs.RemainingBytes();
  DCHECK_GE(all, 1);
  ssize_t sent = 0;
  struct msghdr msg = {};
  while (true) {
    DCHECK_GE(iovecs.RemainingBytes(), 1);
    msg.msg_iov = iovecs.Head();
    msg.msg_iovlen = iovecs.RemainingItems();
    const ssize_t bytes = ::sendmsg(fd.value(), &msg, flags);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      sent += bytes;
      if (iovecs.Advance(bytes)) break;
    } else if (bytes < 0) {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) {
        // Missing MSG_DONTWAIT means SO_SNDTIMEO set on the socket expired.
        if ((flags & MSG_DONTWAIT) && Wait(fd, POLLOUT, deadline)) continue;
        return absl::InternalError(ErrMsg("sendmsg timeout", fd, err));
      }
      return absl::InternalError(ErrMsg("sendmsg", fd, err));
    } else {  // this won't happen.
      DCHECK_EQ(bytes, 0);
      return absl::InternalError(ErrMsg("sendmsg zero", fd));
    }
  }
  DCHECK_EQ(sent, all);
  return absl::OkStatus();
}

absl::Status TcpSocketUtil::Recv(const fd_t fd, IoVecCursor& iovecs,
                                 const int timeout_ms) {
  DCHECK(IsValidSocket(fd));
  DCHECK(IsBlockingMode(fd));
  DCHECK_LE(iovecs.TotalItems(), IOV_MAX);

  const absl::Duration deadline = GetDeadline(timeout_ms);
  const int flags = timeout_ms >= 0 ? MSG_DONTWAIT : 0;
  const ssize_t all = iovecs.RemainingBytes();
  DCHECK_GE(all, 1);
  size_t rcvd = 0;
  struct msghdr msg = {};
  while (true) {
    DCHECK_GE(iovecs.RemainingBytes(), 1);
    msg.msg_iov = iovecs.Head();
    msg.msg_iovlen = iovecs.RemainingItems();
    const ssize_t bytes = ::recvmsg(fd.value(), &msg, flags);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      rcvd += bytes;
      if (iovecs.Advance(bytes)) break;
    } else if (bytes == 0) {  // peer closed connection
      return absl::InternalError(ErrMsg("recvmsg eof", fd));
    } else {
      const Errno err(errno);
      if (Interrupted(err)) continue;
      if (WouldBlock(err)) {
        // Missing MSG_DONTWAIT means SO_RCVTIMEO set on the socket expired.
        if ((flags & MSG_DONTWAIT) && Wait(fd, POLLIN, deadline)) continue;
        return absl::InternalError(ErrMsg("recvmsg timeout", fd, err));
      }
      return absl::InternalError(ErrMsg("recvmsg", fd, err));
    }
  }
  DCHECK_EQ(rcvd, all);
  return absl::OkStatus();
}

}  // namespace peregrine::internal
