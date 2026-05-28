#include "src/internal/socket/socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/internal/socket/ip_util.h"

namespace peregrine::internal {

namespace {
std::string Error(std::string_view msg) {
  return absl::StrFormat("Socket: %s errno=%d(%s)", msg, errno,
                         std::strerror(errno));
}

absl::Status InternalError(std::string_view msg) {
  return absl::InternalError(Error(msg));
}
}  // namespace

absl::StatusOr<int> CreateSocket(int family, int type, bool nonblocking) {
  DCHECK(family == AF_INET || family == AF_INET6);
  DCHECK(type == SOCK_STREAM || type == SOCK_DGRAM);
  const int fd = ::socket(family, type | SOCK_CLOEXEC, /*protocol=*/0);
  if (fd < 0) {
    return InternalError("create");
  }
  DCHECK(IsBlockingMode(fd));
  if (nonblocking) {
    if (const auto status = SetNonBlockingMode(fd); !status.ok()) {
      ::close(fd);  // release the fd.
      return status;
    }
    DCHECK(IsNonBlockingMode(fd));
  }
  return fd;
}

std::string SelfAddrPort(const int fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (getsockname(fd, (struct sockaddr*)&ss, &len) == 0) {
    return ToIpAddrPortString(ss);
  } else {
    return "?";
  }
}

std::string PeerAddrPort(const int fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (getpeername(fd, (struct sockaddr*)&ss, &len) == 0) {
    return ToIpAddrPortString(ss);
  } else if (errno == ENOTCONN) {
    return "*";
  } else {
    return "?";
  }
}

absl::Status SetOption(int fd, int optname, const void* optval,
                       socklen_t optlen) {
  if (setsockopt(fd, SOL_SOCKET, optname, optval, optlen) < 0) {
    return InternalError("setsockopt");
  } else {
    return absl::OkStatus();
  }
}

bool IsBlockingMode(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && !(flags & O_NONBLOCK);
}

bool IsNonBlockingMode(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && (flags & O_NONBLOCK);
}

namespace {
absl::Status SetBlockingMode(int fd, bool nonblocking) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if ABSL_PREDICT_FALSE (flags < 0) {
    return InternalError("fcntl F_GETFL");
  }
  const int v = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if ABSL_PREDICT_FALSE (fcntl(fd, F_SETFL, v) < 0) {
    return InternalError("fcntl F_SETFL");
  } else {
    return absl::OkStatus();
  }
}
}  // namespace

absl::Status SetBlockingMode(int fd) {
  return SetBlockingMode(fd, /*nonblocking=*/false);
}
absl::Status SetNonBlockingMode(int fd) {
  return SetBlockingMode(fd, /*nonblocking=*/true);
}

}  // namespace peregrine::internal
