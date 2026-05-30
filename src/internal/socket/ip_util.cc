#include "src/internal/socket/ip_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

namespace {
std::string PtonErrorMsg(std::string_view ip, int v) {
  return absl::StrFormat("inet_pton failed: ipv%d_addr=%s, errno=%d (%s)", v,
                         ip, errno, std::strerror(errno));
}

std::string NtopErrorMsg(int v) {
  return absl::StrFormat("inet_ntop failed: ipv%d, errno=%d (%s)", v, errno,
                         std::strerror(errno));
}
}  // namespace

std::optional<ipv4_t> ParseIPv4Addr(std::string_view ip) {
  ipv4_t addr;
  switch (inet_pton(AF_INET, std::string(ip).c_str(), &addr)) {
    case 1:
      return addr;
    case 0:
      LOG(WARNING) << "invalid ipv4 addr " << ip;
      return std::nullopt;
    default:
      LOG(WARNING) << PtonErrorMsg(ip, 4);
      return std::nullopt;
  }
}

std::optional<ipv6_t> ParseIPv6Addr(const std::string_view ip) {
  ipv6_t addr;
  switch (inet_pton(AF_INET6, std::string(ip).c_str(), &addr)) {
    case 1:
      return addr;
    case 0:
      LOG(WARNING) << "invalid ipv6 addr " << ip;
      return std::nullopt;
    default:
      LOG(WARNING) << PtonErrorMsg(ip, 6);
      return std::nullopt;
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
