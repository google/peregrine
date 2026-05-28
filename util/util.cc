#include "util/util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdint>
#include <cstring>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"

namespace peregrine::util {

namespace {
using port_t = uint16_t;

template <port_t kMin, port_t kMax>
port_t GenPort(absl::BitGen& bitgen) {
  static_assert(kMin <= kMax);
  return absl::Uniform<port_t>(absl::IntervalClosed, bitgen, kMin, kMax);
}

int Bind(const int fd, const int family, const port_t port) {
  if (family == AF_INET) {
    struct sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    return bind(fd, (struct sockaddr*)&sa, sizeof(sa));
  } else {
    DCHECK_EQ(family, AF_INET6);
    struct sockaddr_in6 sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(port);
    return bind(fd, (struct sockaddr*)&sa, sizeof(sa));
  }
}

port_t GetPort(const struct sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET) {
    return ntohs(reinterpret_cast<const struct sockaddr_in&>(ss).sin_port);
  } else {
    DCHECK_EQ(ss.ss_family, AF_INET6);
    return ntohs(reinterpret_cast<const struct sockaddr_in6&>(ss).sin6_port);
  }
}
}  // namespace

port_t FindFreePort(const int family, const bool tcp) {
  DCHECK(family == AF_INET || family == AF_INET6);
  const int type = tcp ? SOCK_STREAM : SOCK_DGRAM;
  const int protocol = tcp ? IPPROTO_TCP : IPPROTO_UDP;

  absl::BitGen bitgen;
  for (int i = 0; i < 100; ++i) {
    // Create a socket.
    const int fd = socket(family, type, protocol);
    if (fd < 0) {
      continue;
    }

    // Set SO_REUSEADDR to avoid "Address already in use" error.
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
      close(fd);
      continue;
    }

    // Bind the socket to a randomly chosen port.
    constexpr port_t kMinPort = 10'000;
    constexpr port_t kMaxPort = 65'535;
    const port_t port = GenPort<kMinPort, kMaxPort>(bitgen);
    if (Bind(fd, family, port) < 0) {
      close(fd);
      continue;
    }

    // Check the bound socket.
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    std::memset(&ss, 0, len);
    if (getsockname(fd, (struct sockaddr*)&ss, &len) < 0 ||
        ss.ss_family != family || GetPort(ss) != port) {
      close(fd);
      continue;
    }

    // Check that the tcp socket can listen.
    if (tcp && listen(fd, SOMAXCONN) < 0) {
      close(fd);
      continue;
    }

    close(fd);
    return port;
  }

  return 0;
}

}  // namespace peregrine::util
