#include "src/util/interface.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "src/util/nic.h"

namespace peregrine::util {
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

// Returns true iff a raw ip literal is in the given family and is routable.
bool IsRoutable(const std::string& ip, const int family) {
  if (family == AF_INET || family == AF_UNSPEC) {
    struct in_addr ip4;
    if (::inet_pton(AF_INET, ip.c_str(), &ip4) == 1) return IsRoutable(ip4);
  }
  if (family == AF_INET6 || family == AF_UNSPEC) {
    struct in6_addr ip6;
    if (::inet_pton(AF_INET6, ip.c_str(), &ip6) == 1) return IsRoutable(ip6);
  }
  return false;
}

bool Exists(const std::string_view ifname, const std::string_view suffix) {
  const std::string s = absl::StrCat("/sys/class/net/", ifname, suffix);
  return ::access(s.c_str(), F_OK) == 0;
}

// Returns true iff `ifname` is an active bonded master or a physical
// (PCIe-backed) network adapter.
bool IsBondedOrPhysicalInterface(const std::string_view ifname) {
  // TODO: We assume the bonded master interface is assigned ip address but its
  // underlying slave interfaces are not. In general, slave interfaces may also
  // carry configuration or different bonding modes, so this must be revisited.
  //
  // Note: Virtual interfaces (e.g., veth, bridges, or containers) may also be
  // assigned valid ip addresses, so checking for /device may not be sufficient
  // for containerized environments without direct hardware passthrough.
  return Exists(ifname, "/bonding") || Exists(ifname, "/device");
}

// Returns true iff `ifname` is under the RDMA/InfiniBand subsystem.
bool IsRdmaInterface(const std::string_view ifname) {
  return Exists(ifname, "/device/infiniband");
}

}  // namespace

std::vector<std::string> FindRoutableIpAddrs(const int family) {
  std::vector<std::string> addrs;
  for (const auto& [ifname, ips] : EnumerateNics()) {
    if (IsRdmaInterface(ifname)) continue;
    if (!IsBondedOrPhysicalInterface(ifname)) continue;
    for (const auto& ip : ips) {
      if (IsRoutable(ip, family)) {
        addrs.push_back(ip);
      }
    }
  }
  return addrs;
}

}  // namespace peregrine::util
