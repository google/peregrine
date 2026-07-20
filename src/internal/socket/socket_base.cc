#include "src/internal/socket/socket_base.h"

#include <netinet/in.h>

#include "absl/log/check.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

int SocketBase::Bind(const fd_t fd, const Endpoint& local) {
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
