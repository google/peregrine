#include "peregrine/src/util/nic.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "peregrine/src/util/errno.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::util {

namespace {
std::string ErrMsg(const std::string_view msg, const Errno err) {
  return absl::StrFormat("%s errno=%d(%s)", msg, err.value(),
                         std::strerror(err.value()));
}
}  // namespace

absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics() {
  absl::flat_hash_map<std::string, std::vector<std::string>> ifc_ips;

  // Retrieve the interfaces list.
  struct ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) < 0 || interfaces == nullptr) {
    const Errno err(errno);
    LOG(WARNING) << ErrMsg("getifaddrs", err);
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
  if (a >> 16 == 0xA9FE) return false;  // RFC 3927: Link-Local (169.254.0.0/16)
  return true;
}

// Returns true iff the ipv6 address is a unique local unicast address
// (RFC 4193, fc00::/7).
inline bool IN6_IS_ADDR_UniqueLocalUnicast(const struct in6_addr* a) {
  return (a->s6_addr[0] & 0xFE) == 0xFC;
}

// Returns true iff the ipv6 address is routable.
bool IsRoutable(const struct in6_addr& addr) {
  if (IN6_IS_ADDR_UNSPECIFIED(&addr)) return false;
  if (IN6_IS_ADDR_MULTICAST(&addr)) return false;
  if (IN6_IS_ADDR_LINKLOCAL(&addr)) return false;
  if (IN6_IS_ADDR_SITELOCAL(&addr)) return false;
  if (IN6_IS_ADDR_UniqueLocalUnicast(&addr)) return false;
  return true;
}

// Returns the ip addr in the given family if it is routable.
std::optional<util::IpAddr> GetRoutableIpAddr(const std::string& ip,
                                              const int family) {
  if (family == AF_INET || family == AF_UNSPEC) {
    struct in_addr ip4;
    if (inet_pton(AF_INET, ip.c_str(), &ip4) == 1 && IsRoutable(ip4)) {
      return util::IpAddr(ip4);
    }
  }
  if (family == AF_INET6 || family == AF_UNSPEC) {
    struct in6_addr ip6;
    if (inet_pton(AF_INET6, ip.c_str(), &ip6) == 1 && IsRoutable(ip6)) {
      return util::IpAddr(ip6);
    }
  }
  return std::nullopt;
}

// Returns true iff the file at `/sys/class/net/<ifc>/<suffix>` exists.
bool Exists(const std::string_view ifc, const std::string_view suffix) {
  const std::string s = absl::StrCat("/sys/class/net/", ifc, suffix);
  return access(s.c_str(), F_OK) == 0;
}

// Returns true iff the interface `ifc` is a software bridge (e.g., docker0,
// virbr0, gbmcbr).
bool IsBridgeInterface(const std::string_view ifc) {
  return Exists(ifc, "/bridge");
}

// Returns true iff the interface `ifc` is under the RDMA/InfiniBand subsystem.
bool IsRdmaInterface(const std::string_view ifc) {
  return Exists(ifc, "/device/infiniband");
}
}  // namespace

absl::flat_hash_map<std::string, NicInfo> FindRoutableIpAddrs(
    const int family) {
  absl::flat_hash_map<std::string, NicInfo> ifc_ips;
  for (const auto& [ifc, ips] : EnumerateNics()) {
    if (IsBridgeInterface(ifc)) continue;

    std::vector<IpAddr> addrs;
    for (const auto& ip : ips) {
      const std::optional<util::IpAddr> a = GetRoutableIpAddr(ip, family);
      if (a.has_value()) addrs.push_back(*a);
    }
    if (!addrs.empty()) {
      const NicType t = IsRdmaInterface(ifc) ? NicType::kRDMA : NicType::kIP;
      ifc_ips[ifc] = NicInfo{.type = t, .addrs = std::move(addrs)};
    }
  }
  return ifc_ips;
}

std::string ToString(const NicType t) {
  switch (t) {
    case NicType::kIP:
      return "ip";
    case NicType::kRDMA:
      return "rdma";
    default:
      return "invalid";
  }
}

NicType FromString(const std::string_view s) {
  if (absl::EqualsIgnoreCase(s, "ip")) return NicType::kIP;
  if (absl::EqualsIgnoreCase(s, "rdma")) return NicType::kRDMA;
  return NicType::kInvalid;
}

std::string ToString(const NicInfo& ni) {
  return absl::StrCat(
      ToString(ni.type), ": ",
      absl::StrJoin(ni.addrs, ",", [](std::string* out, const IpAddr& ip) {
        absl::StrAppend(out, ip.ToString());
      }));
}

}  // namespace peregrine::util
