#include "src/util/nic.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace peregrine::util {

namespace {
std::string Error(std::string_view msg, int last_errno) {
  return absl::StrFormat("%s errno=%d(%s)", msg, last_errno,
                         std::strerror(last_errno));
}
}  // namespace

absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics() {
  absl::flat_hash_map<std::string, std::vector<std::string>> ifc_ips;

  // Retrieve the interfaces list.
  struct ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) < 0 || interfaces == nullptr) {
    const auto last_errno = errno;
    LOG(WARNING) << Error("getifaddrs", last_errno);
    return ifc_ips;
  }

  // Collect the IP addresses.
  DCHECK_NE(interfaces, nullptr);
  for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    const struct sockaddr* sa = ifa->ifa_addr;
    if (sa == nullptr) continue;

    const int family = sa->sa_family;
    if (family != AF_INET && family != AF_INET6) continue;

    char ip[NI_MAXHOST];
    std::memset(ip, 0, sizeof(ip));
    constexpr int kFlags = NI_NUMERICHOST;
    const size_t salen = (family == AF_INET) ? sizeof(struct sockaddr_in)
                                             : sizeof(struct sockaddr_in6);
    if (getnameinfo(sa, salen, ip, sizeof(ip), nullptr, 0, kFlags) != 0) {
      continue;
    }

    // "fe80::812f:1ecb:8970:a1d4%ens4" -> "fe80::812f:1ecb:8970:a1d4"
    if (char* p = std::strchr(ip, '%'); p != nullptr) *p = '\0';

    // "lo - 127.0.0.1", "lo - ::1"
    // "ens4 - 172.19.255.221", "ens4 - fe80::812f:1ecb:8970:a1d4"
    if (const char* name = ifa->ifa_name; name != nullptr) {
      ifc_ips[name].push_back(ip);
    }
  }

  freeifaddrs(interfaces);
  return ifc_ips;
}

namespace {
// Returns true iff the ipv4 address is routable.
bool IsRoutable(const struct in_addr& addr) {
  const uint32_t a = ntohl(addr.s_addr);
  if (a == 0) return false;             // INADDR_ANY
  if (a >> 28 == 0xE) return false;     // RFC 1112: Multicast (224.0.0.0/4)
  if (a >> 24 == 0x7F) return false;    // RFC 1122: Loopback (127.0.0.0/8)
  if (a >> 16 == 0xA9FE) return false;  // RFC 3927: Link-Local (169.254.0.0/16)
  return true;
}

// Returns true iff the ipv6 address is routable.
bool IsRoutable(const struct in6_addr& addr) {
  if (IN6_IS_ADDR_UNSPECIFIED(&addr)) return false;
  if (IN6_IS_ADDR_MULTICAST(&addr)) return false;
  if (IN6_IS_ADDR_LOOPBACK(&addr)) return false;
  if (IN6_IS_ADDR_LINKLOCAL(&addr)) return false;
  if (IN6_IS_ADDR_SITELOCAL(&addr)) return false;
  return true;
}

// Returns true iff the ip addr is in the given family and is routable.
bool IsRoutable(const std::string& ip, const int family) {
  if (family == AF_INET || family == AF_UNSPEC) {
    struct in_addr ip4;
    if (inet_pton(AF_INET, ip.c_str(), &ip4) == 1) return IsRoutable(ip4);
  }
  if (family == AF_INET6 || family == AF_UNSPEC) {
    struct in6_addr ip6;
    if (inet_pton(AF_INET6, ip.c_str(), &ip6) == 1) return IsRoutable(ip6);
  }
  return false;
}

// Returns true iff the file at `/sys/class/net/<ifc>/<suffix>` exists.
bool Exists(const std::string_view ifc, const std::string_view suffix) {
  const std::string s = absl::StrCat("/sys/class/net/", ifc, suffix);
  return access(s.c_str(), F_OK) == 0;
}

// Returns true iff the interface `ifc` is a physical network adapter.
bool IsPhysicalInterface(const std::string_view ifc) {
  // Note: Virtual interfaces (e.g., veth, bridges, or containers) may also be
  // assigned valid ip addresses, so checking for /device may not be sufficient
  // for containerized environments without direct hardware passthrough.
  return Exists(ifc, "/device");
}

// Returns true iff `ifc` is an active bonded master network adapter.
bool IsBondedInterface(const std::string_view ifc) {
  // TODO: We assume the bonded master interface is assigned ip address but its
  // underlying slave interfaces are not. In general, slave interfaces may also
  // carry configuration or different bonding modes, so this must be revisited.
  return Exists(ifc, "/bonding");
}

// Returns true iff the interface `ifc` is under the RDMA/InfiniBand subsystem.
bool IsRdmaInterface(const std::string_view ifc) {
  return Exists(ifc, "/device/infiniband");
}
}  // namespace

absl::flat_hash_map<std::string, std::vector<std::string>> FindRoutableIpAddrs(
    const int family) {
  absl::flat_hash_map<std::string, std::vector<std::string>> ifc_ips;
  for (const auto& [ifc, ips] : EnumerateNics()) {
    if (!IsPhysicalInterface(ifc) && !IsBondedInterface(ifc)) continue;
    if (IsRdmaInterface(ifc)) continue;
    for (const auto& ip : ips) {
      if (IsRoutable(ip, family)) {
        ifc_ips[ifc].push_back(ip);
      }
    }
  }
  return ifc_ips;
}

}  // namespace peregrine::util
