#include "peregrine/src/internal/socket/socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal {

fd_t CreateSocket(int family, int type, bool blocking) {
  DCHECK(family == AF_INET || family == AF_INET6);
  DCHECK(type == SOCK_STREAM || type == SOCK_DGRAM);

  const int ret = ::socket(family, type | SOCK_CLOEXEC, /*protocol=*/0);
  if ABSL_PREDICT_FALSE (ret < 0) {
    const int last_errno = errno;
    LOG(WARNING) << ErrorMsg("socket", last_errno);
    return fd_t(-1);
  }

  const fd_t fd(ret);
  DCHECK(IsBlockingMode(fd));
  if (blocking) return fd;

  if (!SetNonBlockingMode(fd)) {
    ::close(fd.value());  // release the socket resource.
    return fd_t(-1);
  }
  DCHECK(IsNonBlockingMode(fd));
  return fd;
}

bool IsBlockingMode(fd_t fd) {
  const int flags = ::fcntl(fd.value(), F_GETFL);
  return flags >= 0 && !(flags & O_NONBLOCK);
}

bool IsNonBlockingMode(fd_t fd) {
  const int flags = ::fcntl(fd.value(), F_GETFL);
  return flags >= 0 && (flags & O_NONBLOCK);
}

bool __set_blocking_mode(fd_t fd, bool blocking) {
  const int flags = ::fcntl(fd.value(), F_GETFL);
  if ABSL_PREDICT_FALSE (flags < 0) return false;
  const int cmd = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return ::fcntl(fd.value(), F_SETFL, cmd) >= 0;
}

std::string SelfAddrPort(const fd_t fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (::getsockname(fd.value(), (struct sockaddr*)&ss, &len) == 0) {
    return ToIpAddrPortString(ss);
  } else {
    const int last_errno = errno;
    LOG(WARNING) << ErrorMsg("getsockname", last_errno);
    return "?";
  }
}

std::string PeerAddrPort(const fd_t fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (::getpeername(fd.value(), (struct sockaddr*)&ss, &len) == 0) {
    return ToIpAddrPortString(ss);
  } else if (errno == ENOTCONN) {
    return "*";
  } else {
    const int last_errno = errno;
    LOG(WARNING) << ErrorMsg("getpeername", last_errno);
    return "?";
  }
}

Endpoint SelfEndpoint(const fd_t fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (::getsockname(fd.value(), (struct sockaddr*)&ss, &len) == 0) {
    return Endpoint::Create(ss);
  } else {
    const int last_errno = errno;
    LOG(WARNING) << ErrorMsg("getsockname", last_errno);
    return Endpoint();
  }
}

Endpoint PeerEndpoint(const fd_t fd) {
  struct sockaddr_storage ss;
  socklen_t len = sizeof(ss);
  if (::getpeername(fd.value(), (struct sockaddr*)&ss, &len) == 0) {
    return Endpoint::Create(ss);
  } else {
    const int last_errno = errno;
    LOG(WARNING) << ErrorMsg("getpeername", last_errno);
    return Endpoint();
  }
}

std::string ToIpAddrPortString(const struct sockaddr_storage& ss) {
  switch (ss.ss_family) {
    case AF_INET: {
      const auto* sa = reinterpret_cast<const struct sockaddr_in*>(&ss);
      const std::string s = util::ToIPv4String(sa->sin_addr);
      return absl::StrCat(s, ":", ntohs(sa->sin_port));
    }
    case AF_INET6: {
      const auto* sa = reinterpret_cast<const struct sockaddr_in6*>(&ss);
      const std::string s = util::ToIPv6String(sa->sin6_addr);
      return absl::StrCat("[", s, "]:", ntohs(sa->sin6_port));
    }
    default:
      return absl::StrCat("invalid addr family: ", ss.ss_family);
  }
}

}  // namespace peregrine::internal
