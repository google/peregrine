#include "src/internal/base/endpoint.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"

namespace peregrine::internal {

namespace {
inline bool LooksLikeIPv6(std::string_view addr) {
  return addr.starts_with('[') && addr.ends_with(']');
}
}  // namespace

Endpoint Endpoint::Create(const std::string_view ipaddr_port) {
  // "127.0.0.1:12345" or "[::1]:12345", or sth invalid
  const Endpoint invalid;
  DCHECK(!invalid.IsValid());

  const auto pos = ipaddr_port.rfind(':');
  if (pos == std::string_view::npos) {
    LOG(WARNING) << "invalid ip:port " << ipaddr_port;
    return invalid;
  }

  const std::string_view a = ipaddr_port.substr(0, pos);
  const std::string_view p = ipaddr_port.substr(pos + 1);

  uint16_t port;
  if (!(absl::SimpleAtoi(p, &port) && 1 <= port && port <= 65535)) {
    LOG(WARNING) << "invalid port in " << ipaddr_port;
    return invalid;
  }

  std::string_view ipaddr = a;
  if (LooksLikeIPv6(ipaddr)) {
    ipaddr = a.substr(1, a.size() - 2);  // "[...]" -> "..."
    const std::optional<ipv6_t> ipv6 = ParseIPv6Addr(ipaddr);
    if (!ipv6.has_value()) {
      LOG(WARNING) << "invalid ipv6 addr in " << ipaddr_port;
      return invalid;
    }
    const Endpoint e(ipv6.value(), port);
    DCHECK(e.IsValid());
    return e;

  } else {
    const std::optional<ipv4_t> ipv4 = ParseIPv4Addr(ipaddr);
    if (!ipv4.has_value()) {
      LOG(WARNING) << "invalid ipv4 addr in " << ipaddr_port;
      return invalid;
    }
    const Endpoint e(ipv4.value(), port);
    DCHECK(e.IsValid());
    return e;
  }
}

bool operator==(const Endpoint& a, const Endpoint& b) {
  if (a.port_ != b.port_) {
    return false;
  } else if (IsIPv4(a.ipaddr_) && IsIPv4(b.ipaddr_)) {
    const ipv4_t& a4 = std::get<ipv4_t>(a.ipaddr_);
    const ipv4_t& b4 = std::get<ipv4_t>(b.ipaddr_);
    return a4.s_addr == b4.s_addr;
  } else if (IsIPv6(a.ipaddr_) && IsIPv6(b.ipaddr_)) {
    const ipv6_t& a6 = std::get<ipv6_t>(a.ipaddr_);
    const ipv6_t& b6 = std::get<ipv6_t>(b.ipaddr_);
    return memcmp(&a6, &b6, sizeof(ipv6_t)) == 0;
  } else {
    return false;
  }
}

std::string Endpoint::ToString() const {
  if (IsIPv4(ipaddr_)) {
    const ipv4_t& ip4 = std::get<ipv4_t>(ipaddr_);
    return absl::StrCat(ToIPv4String(ip4), ":", port_);
  } else {
    DCHECK(IsIPv6(ipaddr_));
    const ipv6_t& ip6 = std::get<ipv6_t>(ipaddr_);
    return absl::StrCat("[", ToIPv6String(ip6), "]:", port_);
  }
};

}  // namespace peregrine::internal
