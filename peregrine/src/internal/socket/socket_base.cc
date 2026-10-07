#include "peregrine/src/internal/socket/socket_base.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"

namespace peregrine::internal {

namespace {
// Sets the socket file descriptor `fd` to the specified blocking mode.
// Returns 0 on success, -1 on error.
// Note: we put this function here instead of in socket_util.h because we want
// to set blocking mode only through the socket, not via its file descriptor,
// to avoid accidental misuse of an utility function.
int SetSocketBlockingMode(const fd_t fd, const bool blocking) {
  const int flags = ::fcntl(fd.value(), F_GETFL);
  if ABSL_PREDICT_FALSE (flags < 0) return -1;
  const int cmd = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return ::fcntl(fd.value(), F_SETFL, cmd);
}
}  // namespace

int SocketBase::SetBlocking(const bool blocking) {
  if (blocking == blocking_) {
    DCHECK(MatchesBlocking());
    return 0;
  }
  if (SetSocketBlockingMode(fd_, blocking) == 0) {
    blocking_ = blocking;
    DCHECK(MatchesBlocking());
    return 0;
  }
  DCHECK(MatchesBlocking());
  return -1;
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
