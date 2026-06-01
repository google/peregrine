#include "src/internal/base/ipaddr.h"

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
#include "absl/strings/str_format.h"

namespace peregrine::internal {

namespace {
inline std::string PtonErrorMsg(std::string_view ip, int v) {
  return absl::StrFormat("inet_pton failed: ipv%d_addr=%s, errno=%d (%s)", v,
                         ip, errno, std::strerror(errno));
}

inline std::string NtopErrorMsg() {
  return absl::StrFormat("inet_ntop failed: errno=%d (%s)", errno,
                         std::strerror(errno));
}
}  // namespace

std::optional<ipv4_t> ParseIPv4Addr(std::string_view ip) {
  ipv4_t addr;
  switch (inet_pton(AF_INET, std::string(ip).c_str(), &addr)) {
    case 1:
      return addr;
    case 0:
      LOG(WARNING) << "invalid ipv4 addr: " << ip;
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
      LOG(WARNING) << "invalid ipv6 addr: " << ip;
      return std::nullopt;
    default:
      LOG(WARNING) << PtonErrorMsg(ip, 6);
      return std::nullopt;
  }
}

std::string ToIPv4String(const ipv4_t& ip4) {
  char addr[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &ip4, addr, INET_ADDRSTRLEN) != nullptr) {
    return addr;
  } else {
    LOG(WARNING) << NtopErrorMsg();
    return "invalid ipv4 addr";
  }
}

std::string ToIPv6String(const ipv6_t& ip6) {
  char addr[INET6_ADDRSTRLEN];
  if (inet_ntop(AF_INET6, &ip6, addr, INET6_ADDRSTRLEN) != nullptr) {
    return addr;
  } else {
    LOG(WARNING) << NtopErrorMsg();
    return "invalid ipv6 addr";
  }
}

std::optional<IpAddr> Create(std::string_view ip) {
  if (auto v4 = ParseIPv4Addr(ip); v4.has_value()) {
    return IpAddr(v4.value());
  } else if (auto v6 = ParseIPv6Addr(ip); v6.has_value()) {
    return IpAddr(v6.value());
  } else {
    return std::nullopt;
  }
}

bool operator==(const IpAddr& a, const IpAddr& b) {
  if (a.IsIPv4() && b.IsIPv4()) {
    const ipv4_t& a4 = a.IPv4Addr();
    const ipv4_t& b4 = b.IPv4Addr();
    return a4.s_addr == b4.s_addr;

  } else if (a.IsIPv6() && b.IsIPv6()) {
    const ipv6_t& a6 = a.IPv6Addr();
    const ipv6_t& b6 = b.IPv6Addr();
    return memcmp(&a6, &b6, sizeof(ipv6_t)) == 0;

  } else {
    return false;
  }
}

}  // namespace peregrine::internal
