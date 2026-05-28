#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

namespace {
absl::Status InvalidArgumentError(const std::string_view msg,
                                  const std::string_view arg) {
  return absl::InvalidArgumentError(absl::StrCat(msg, " ", arg));
}

std::string InetNtopError() {
  return absl::StrFormat("inet_ntop failed: errno=%d (%s)", errno,
                         std::strerror(errno));
}
}  // namespace

absl::StatusOr<ipv4_t> ParseIPv4Addr(std::string_view ip) {
  ipv4_t addr;
  if (inet_pton(AF_INET, std::string(ip).c_str(), &addr) == 1) {
    return addr;
  } else {
    return InvalidArgumentError("invalid ipv4 addr ", ip);
  }
}

absl::StatusOr<ipv6_t> ParseIPv6Addr(const std::string_view ip) {
  ipv6_t addr;
  if (inet_pton(AF_INET6, std::string(ip).c_str(), &addr) == 1) {
    return addr;
  } else {
    return InvalidArgumentError("invalid ipv6 addr ", ip);
  }
}

namespace {
template <int kFamily, int kAddrLen, typename T>
std::string ToString(const struct sockaddr_storage& ss) {
  char addr[kAddrLen];
  const T* sa = reinterpret_cast<const T*>(&ss);
  if constexpr (kFamily == AF_INET) {
    if (inet_ntop(AF_INET, &sa->sin_addr, addr, kAddrLen) != nullptr) {
      return absl::StrCat(addr, ":", ntohs(sa->sin_port));
    }
  } else {
    static_assert(kFamily == AF_INET6);
    if (inet_ntop(AF_INET6, &sa->sin6_addr, addr, kAddrLen) != nullptr) {
      return absl::StrCat("[", addr, "]:", ntohs(sa->sin6_port));
    }
  }
  return InetNtopError();
}
}  // namespace

std::string ToIpAddrPortString(const struct sockaddr_storage& ss) {
  switch (ss.ss_family) {
    case AF_INET:
      return ToString<AF_INET, INET_ADDRSTRLEN, struct sockaddr_in>(ss);
    case AF_INET6:
      return ToString<AF_INET6, INET6_ADDRSTRLEN, struct sockaddr_in6>(ss);
    default:
      return absl::StrFormat("non-ip address family: %d", ss.ss_family);
  }
}

struct sockaddr_in BuildIPv4Sockaddr(const IpAddr& ip, const port_t port) {
  DCHECK(IsIPv4(ip));
  return sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = std::get<ipv4_t>(ip),
  };
}

struct sockaddr_in6 BuildIPv6Sockaddr(const IpAddr& ip, const port_t port) {
  DCHECK(IsIPv6(ip));
  return sockaddr_in6{
      .sin6_family = AF_INET6,
      .sin6_port = htons(port),
      .sin6_addr = std::get<ipv6_t>(ip),
  };
}

}  // namespace peregrine::internal
