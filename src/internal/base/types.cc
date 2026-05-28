#include "src/internal/base/types.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace peregrine::internal {

namespace {
std::string InetNtopError() {
  return absl::StrFormat("inet_ntop failed: errno=%d (%s)", errno,
                         std::strerror(errno));
}
}  // namespace

std::string ToIPv4String(const ipv4_t& ip4) {
  char addr[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &ip4, addr, INET_ADDRSTRLEN) != nullptr) {
    return addr;
  } else {
    LOG(WARNING) << InetNtopError();
    return "invalid ipv4 addr";
  }
}

std::string ToIPv6String(const ipv6_t& ip6) {
  char addr[INET6_ADDRSTRLEN];
  if (inet_ntop(AF_INET6, &ip6, addr, INET6_ADDRSTRLEN) != nullptr) {
    return addr;
  } else {
    LOG(WARNING) << InetNtopError();
    return "invalid ipv6 addr";
  }
}

std::string ToString(const IpAddr& ip) {
  DCHECK(IsIPv4(ip) || IsIPv6(ip));
  return IsIPv4(ip) ? ToIPv4String(std::get<ipv4_t>(ip))
                    : ToIPv6String(std::get<ipv6_t>(ip));
}

}  // namespace peregrine::internal
