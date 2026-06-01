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
#include "absl/strings/str_format.h"

namespace peregrine::internal {

int CreateSocket(int family, int type, bool nonblocking) {
  DCHECK(family == AF_INET || family == AF_INET6);
  DCHECK(type == SOCK_STREAM || type == SOCK_DGRAM);
  const int fd = ::socket(family, type | SOCK_CLOEXEC, /*protocol=*/0);
  if (fd < 0) {
    LOG(WARNING) << ErrorMsg("socket");
    return -1;
  }
  DCHECK(IsBlockingMode(fd));
  if (!nonblocking) {
    return fd;
  }
  if (!SetNonBlockingMode(fd)) {
    close(fd);  // release the socket resource.
    return -1;
  }
  DCHECK(IsNonBlockingMode(fd));
  return fd;
}

bool IsBlockingMode(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && !(flags & O_NONBLOCK);
}

bool IsNonBlockingMode(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && (flags & O_NONBLOCK);
}

bool __set_blocking_mode(int fd, bool nonblocking) {
  const int flags = fcntl(fd, F_GETFL, /*cmd*/ 0);
  if ABSL_PREDICT_FALSE (flags < 0) {
    return false;
  }
  const int cmd = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  return fcntl(fd, F_SETFL, cmd) >= 0;
}

std::string SelfAddrPort(const int fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (getsockname(fd, (struct sockaddr*)&ss, &len) == 0) {
    return ToIpAddrPortString(ss);
  } else {
    LOG(WARNING) << ErrorMsg("getsockname");
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
    LOG(WARNING) << ErrorMsg("getpeername");
    return "?";
  }
}

namespace {
std::string NtopErrorMsg(int v) {
  return absl::StrFormat("inet_ntop failed: ipv%d, errno=%d (%s)", v, errno,
                         std::strerror(errno));
}
}  // namespace

namespace {
template <int kFamily, int kAddrLen, typename T>
std::string ToString(const struct sockaddr_storage& ss) {
  char addr[kAddrLen];
  const T* sa = reinterpret_cast<const T*>(&ss);
  if constexpr (kFamily == AF_INET) {
    if (inet_ntop(AF_INET, &sa->sin_addr, addr, kAddrLen) != nullptr) {
      return absl::StrCat(addr, ":", ntohs(sa->sin_port));
    }
    LOG(WARNING) << NtopErrorMsg(4);
    return "invalid ipv4:port";
  } else {
    static_assert(kFamily == AF_INET6);
    if (inet_ntop(AF_INET6, &sa->sin6_addr, addr, kAddrLen) != nullptr) {
      return absl::StrCat("[", addr, "]:", ntohs(sa->sin6_port));
    }
    LOG(WARNING) << NtopErrorMsg(6);
    return "invalid ipv6:port";
  }
}
}  // namespace

std::string ToIpAddrPortString(const struct sockaddr_storage& ss) {
  switch (ss.ss_family) {
    case AF_INET:
      return ToString<AF_INET, INET_ADDRSTRLEN, struct sockaddr_in>(ss);
    case AF_INET6:
      return ToString<AF_INET6, INET6_ADDRSTRLEN, struct sockaddr_in6>(ss);
    default:
      return absl::StrCat("invalid addr family: ", ss.ss_family);
  }
}

}  // namespace peregrine::internal
