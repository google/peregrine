#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/types.h"

namespace peregrine {

int AddressFamily(ipaddr_t ip) {
  if (IsIPv4Addr(ip)) return AF_INET;
  if (IsIPv6Addr(ip)) return AF_INET6;
  return AF_UNSPEC;
}

bool IsIPv4Addr(const ipaddr_t ip) {
  struct in_addr addr;
  return inet_pton(AF_INET, std::string(ip).c_str(), &addr) == 1;
}

bool IsIPv6Addr(const ipaddr_t ip) {
  struct in6_addr addr;
  return inet_pton(AF_INET6, std::string(ip).c_str(), &addr) == 1;
}

struct in_addr ParseIPv4Addr(const ipaddr_t ip) {
  struct in_addr addr;
  if (inet_pton(AF_INET, std::string(ip).c_str(), &addr) == 1) {
    return addr;
  } else {
    return {.s_addr = INADDR_ANY};
  }
}

struct in6_addr ParseIPv6Addr(const ipaddr_t ip) {
  struct in6_addr addr;
  if (inet_pton(AF_INET6, std::string(ip).c_str(), &addr) == 1) {
    return addr;
  } else {
    return IN6ADDR_ANY_INIT;
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
  return absl::StrFormat("inet_ntop failed: errno=%d(%s)", errno,
                         std::strerror(errno));
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

struct sockaddr_in BuildIPv4Sockaddr(const ipaddr_t ip, const port_t port) {
  DCHECK(IsIPv4Addr(ip));
  return sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = ParseIPv4Addr(ip),
  };
}

struct sockaddr_in6 BuildIPv6Sockaddr(const ipaddr_t ip, const port_t port) {
  DCHECK(IsIPv6Addr(ip));
  return sockaddr_in6{
      .sin6_family = AF_INET6,
      .sin6_port = htons(port),
      .sin6_addr = ParseIPv6Addr(ip),
  };
}

}  // namespace peregrine
