#include "peregrine/src/util/ipaddr.h"

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
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::util {

namespace {
std::string PtonErrMsg(const std::string_view ip, const int v,
                       const Errno err) {
  return absl::StrFormat("inet_pton failed: ipv%d_addr=%s, errno=%d (%s)", v,
                         ip, err.value(), std::strerror(err.value()));
}

std::string NtopErrMsg(const Errno err) {
  return absl::StrFormat("inet_ntop failed: errno=%d (%s)", err.value(),
                         std::strerror(err.value()));
}
}  // namespace

bool IpAddr::IsZero() const {
  if (IsIPv4()) {
    return IPv4Addr().s_addr == 0;
  } else {
    DCHECK(IsIPv6());
    return IN6_IS_ADDR_UNSPECIFIED(&IPv6Addr());
  }
}

bool IpAddr::IsLoopback() const {
  if (IsIPv4()) {
    // RFC 1122: 127.0.0.0/8
    return (ntohl(IPv4Addr().s_addr) >> 24) == 0x7F;
  } else {
    DCHECK(IsIPv6());
    // RFC 4291: ::1 or IPv4-mapped ::ffff:127.0.0.0/8
    const ipv6_t& ip6 = IPv6Addr();
    if (IN6_IS_ADDR_LOOPBACK(&ip6)) return true;
    if (IN6_IS_ADDR_V4MAPPED(&ip6)) return ip6.s6_addr[12] == 0x7F;
    return false;
  }
}

std::optional<ipv4_t> ParseIPv4Addr(std::string_view ip) {
  ipv4_t addr;
  switch (inet_pton(AF_INET, std::string(ip).c_str(), &addr)) {
    case 1:
      return addr;
    case 0:
      LOG(WARNING) << "invalid ipv4 addr: " << ip;
      return std::nullopt;
    default:
      const Errno err(errno);
      LOG(WARNING) << PtonErrMsg(ip, 4, err);
      return std::nullopt;
  }
}

std::optional<ipv6_t> ParseIPv6Addr(const std::string_view ip) {
  ipv6_t addr;
  switch (inet_pton(AF_INET6, std::string(ip).c_str(), &addr)) {
    case 1:
      return addr;
    case 0:
      LOG(WARNING) << "invalid ipv6 addr: " << ip;
      return std::nullopt;
    default:
      const Errno err(errno);
      LOG(WARNING) << PtonErrMsg(ip, 6, err);
      return std::nullopt;
  }
}

std::string ToIPv4String(const ipv4_t& ip4) {
  constexpr int kAddrLen = INET_ADDRSTRLEN;
  char addr[kAddrLen];
  if (inet_ntop(AF_INET, &ip4, addr, kAddrLen) != nullptr) {
    return addr;
  } else {
    const Errno err(errno);
    LOG(WARNING) << NtopErrMsg(err);
    return "invalid ipv4 addr";
  }
}

std::string ToIPv6String(const ipv6_t& ip6) {
  constexpr int kAddrLen = INET6_ADDRSTRLEN;
  char addr[kAddrLen];
  if (inet_ntop(AF_INET6, &ip6, addr, kAddrLen) != nullptr) {
    return addr;
  } else {
    const Errno err(errno);
    LOG(WARNING) << NtopErrMsg(err);
    return "invalid ipv6 addr";
  }
}

std::optional<IpAddr> IpAddr::Create(std::string_view ip) {
  // IPv6 literals always contain ':' while IPv4 literals never do.
  if (absl::StrContains(ip, ':')) {
    const auto v6 = ParseIPv6Addr(ip);
    if (v6.has_value()) return IpAddr(v6.value());
  } else {
    const auto v4 = ParseIPv4Addr(ip);
    if (v4.has_value()) return IpAddr(v4.value());
  }
  return std::nullopt;
}

bool operator==(const IpAddr& a, const IpAddr& b) {
  if (a.IsIPv4() && b.IsIPv4()) {
    return a.IPv4Addr().s_addr == b.IPv4Addr().s_addr;
  } else if (a.IsIPv6() && b.IsIPv6()) {
    return std::memcmp(&a.IPv6Addr(), &b.IPv6Addr(), sizeof(ipv6_t)) == 0;
  } else {
    return false;
  }
}

std::string IpAddr::ToString() const {
  return IsIPv4() ? ToIPv4String(IPv4Addr()) : ToIPv6String(IPv6Addr());
}

}  // namespace peregrine::util
