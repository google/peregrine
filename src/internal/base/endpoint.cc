#include "src/internal/base/endpoint.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "src/internal/base/ipaddr.h"

namespace peregrine::internal {

namespace {
inline bool LooksLikeIPv6(std::string_view addr) {
  return addr.starts_with('[') && addr.ends_with(']');
}
}  // namespace

Endpoint Endpoint::Create(const std::string_view ipaddr_port) {
  // "127.0.0.1:56789" or "[::1]:56789", or sth invalid
  const Endpoint empty;

  const auto pos = ipaddr_port.rfind(':');
  if (pos == std::string_view::npos) {
    LOG(WARNING) << "invalid ip:port " << ipaddr_port;
    return empty;
  }

  const std::string_view a = ipaddr_port.substr(0, pos);
  const std::string_view p = ipaddr_port.substr(pos + 1);

  int port = -1;
  if (!(absl::SimpleAtoi(p, &port) && 0 <= port && port <= 65535)) {
    LOG(WARNING) << "invalid port in " << ipaddr_port;
    return empty;
  }

  std::string_view ipaddr = a;
  if (LooksLikeIPv6(ipaddr)) {
    ipaddr = a.substr(1, a.size() - 2);  // "[...]" -> "..."
    const std::optional<ipv6_t> ipv6 = ParseIPv6Addr(ipaddr);
    if (!ipv6.has_value()) {
      LOG(WARNING) << "invalid ipv6 addr in " << ipaddr_port;
      return empty;
    }
    return Endpoint(ipv6.value(), port);

  } else {
    const std::optional<ipv4_t> ipv4 = ParseIPv4Addr(ipaddr);
    if (!ipv4.has_value()) {
      LOG(WARNING) << "invalid ipv4 addr in " << ipaddr_port;
      return empty;
    }
    return Endpoint(ipv4.value(), port);
  }
}

struct sockaddr_in Endpoint::BuildIPv4Sockaddr() const {
  DCHECK(IsIPv4());
  return sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(port_),
      .sin_addr = IPv4Addr(),
  };
}

struct sockaddr_in6 Endpoint::BuildIPv6Sockaddr() const {
  DCHECK(IsIPv6());
  return sockaddr_in6{
      .sin6_family = AF_INET6,
      .sin6_port = htons(port_),
      .sin6_addr = IPv6Addr(),
  };
}

std::string Endpoint::ToString() const {
  const std::string addr = ipaddr_.ToString();
  if (ipaddr_.IsIPv4()) {
    return absl::StrCat(addr, ":", port_);
  } else {
    DCHECK(ipaddr_.IsIPv6());
    return absl::StrCat("[", addr, "]:", port_);
  }
};

}  // namespace peregrine::internal
