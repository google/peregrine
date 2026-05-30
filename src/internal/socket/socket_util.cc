#include "src/internal/socket/socket_util.h"

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
#include "src/internal/socket/ip_util.h"

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

}  // namespace peregrine::internal
