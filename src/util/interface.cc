#include "src/util/interface.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/util/nic.h"

namespace peregrine::util {
namespace {

// Evaluates if an IPv4 address is routable (excluding loopback 127.0.0.0/8,
// link-local 169.254.0.0/16, and multicast 224.0.0.0/4).
bool IsRoutableIpv4(const struct in_addr& addr) {
  if (addr.s_addr == INADDR_ANY) return false;
  const uint32_t a = ntohl(addr.s_addr);
  return (a & 0xFF000000) != 0x7F000000 &&  // RFC 1122: Loopback (127.0.0.0/8)
         (a & 0xFFFF0000) !=
             0xA9FE0000 &&  // RFC 3927: Link-Local (169.254.0.0/16)
         !IN_MULTICAST(a);  // RFC 1112: Multicast (224.0.0.0/4)
}

// Evaluates if an IPv6 address is routable (excluding loopback ::1, link-local
// fe80::/10, site-local fec0::/10, and multicast ff00::/8).
bool IsRoutableIpv6(const struct in6_addr& addr) {
  return !IN6_IS_ADDR_UNSPECIFIED(&addr) && !IN6_IS_ADDR_LOOPBACK(&addr) &&
         !IN6_IS_ADDR_LINKLOCAL(&addr) && !IN6_IS_ADDR_SITELOCAL(&addr) &&
         !IN6_IS_ADDR_MULTICAST(&addr);
}

// Evaluates if a raw IP literal matches the given family filter and is
// routable.
bool IsRoutableIpAddress(const std::string& raw_ip, int family) {
  if (family == AF_INET || family == AF_UNSPEC) {
    struct in_addr in4;
    if (::inet_pton(AF_INET, raw_ip.c_str(), &in4) == 1) {
      return IsRoutableIpv4(in4);
    }
  }

  if (family == AF_INET6 || family == AF_UNSPEC) {
    struct in6_addr in6;
    if (::inet_pton(AF_INET6, raw_ip.c_str(), &in6) == 1) {
      return IsRoutableIpv6(in6);
    }
  }

  return false;
}

// Returns true iff `ifname` corresponds to an active bonded master or a
// physical (PCIe-backed) network adapter.
//
// TODO: We currently assume the bonded master interface is
// assigned the IP address and its underlying slave interfaces are not. In
// general, slave interfaces may also carry configuration or different
// bonding modes, so this should be refined in the future.
//
// Note: Virtual interfaces (e.g., veth, bridges, or containers) may also be
// assigned valid IP addresses, so checking for /device may not be sufficient
// if running in containerized environments without direct hardware passthrough.
bool IsBondedOrPhysicalInterface(absl::string_view ifname) {
  // Admit active Link-Aggregated Bonding Masters (e.g., 'eth0').
  if (::access(absl::StrCat("/sys/class/net/", ifname, "/bonding").c_str(),
               F_OK) == 0) {
    return true;
  }

  // Reject Virtual Containers, Bridges, and Loopbacks lacking direct PCIe
  // anchors (/sys/class/net/<name>/device).
  const std::string sys_net_device =
      absl::StrCat("/sys/class/net/", ifname, "/device");
  return ::access(sys_net_device.c_str(), F_OK) == 0;
}

// Returns true iff `ifname` operates under the Linux RDMA/InfiniBand subsystem.
bool IsRdmaInterface(absl::string_view ifname) {
  const std::string sys_net_infiniband =
      absl::StrCat("/sys/class/net/", ifname, "/device/infiniband");
  return ::access(sys_net_infiniband.c_str(), F_OK) == 0;
}

}  // namespace

std::vector<std::string> EnumerateIpInterfaces(int family) {
  std::vector<std::string> ips;

  for (const auto& [ifname, ip_strings] : EnumerateNics()) {
    if (!IsBondedOrPhysicalInterface(ifname) || IsRdmaInterface(ifname)) {
      continue;
    }

    for (const std::string& raw_ip : ip_strings) {
      if (IsRoutableIpAddress(raw_ip, family)) {
        ips.push_back(raw_ip);
      }
    }
  }

  return ips;
}

}  // namespace peregrine::util
