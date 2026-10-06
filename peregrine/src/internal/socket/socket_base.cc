#include "peregrine/src/internal/socket/socket_base.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "absl/log/check.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"

namespace peregrine::internal {

bool SocketBase::IsBlocking() const {
  if (IsBlockingMode(fd_)) {
    DCHECK(!IsNonBlockingMode(fd_));
    return true;
  } else {
    DCHECK(IsNonBlockingMode(fd_));
    return false;
  }
}

bool SocketBase::IsNonBlocking() const {
  if (IsNonBlockingMode(fd_)) {
    DCHECK(!IsBlockingMode(fd_));
    return true;
  } else {
    DCHECK(IsBlockingMode(fd_));
    return false;
  }
}

int SocketBase::Bind(const fd_t fd, const Endpoint& local) {
  DCHECK(IsValidSocket(fd));

  if (local.IsIPv4()) {
    const struct sockaddr_in sa = local.BuildIPv4Sockaddr();
    return ::bind(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
  } else {
    DCHECK(local.IsIPv6());
    const struct sockaddr_in6 sa = local.BuildIPv6Sockaddr();
    return ::bind(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
  }
}

int SocketBase::Connect(const fd_t fd, const Endpoint& peer) {
  DCHECK(IsValidSocket(fd));
  DCHECK(peer.HasNonzeroIpPort());

  if (peer.IsIPv4()) {
    const struct sockaddr_in sa = peer.BuildIPv4Sockaddr();
    return ::connect(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
  } else {
    DCHECK(peer.IsIPv6());
    const struct sockaddr_in6 sa = peer.BuildIPv6Sockaddr();
    return ::connect(fd.value(), (struct sockaddr*)&sa, sizeof(sa));
  }
}

}  // namespace peregrine::internal
