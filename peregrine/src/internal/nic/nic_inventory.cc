#include "peregrine/src/internal/nic/nic_inventory.h"

#include <ifaddrs.h>
#include <infiniband/verbs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/nic/pci_topology.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal::nic {
namespace {

uint64_t GetSortKey(const PciLocation& loc) {
  if (loc.endpoint_bdf.empty()) return UINT64_MAX;
  return ParsePciBdfKey(loc.endpoint_bdf).value_or(UINT64_MAX - 1);
}

bool IsUsableIp(const util::IpAddr& ip, bool allow_loopback) {
  return ip.IsRoutable() && (allow_loopback || !ip.IsLoopback());
}

bool IsRdmaNetdev(std::string_view if_name) {
  const std::string path =
      absl::StrCat("/sys/class/net/", if_name, "/device/infiniband");
  return ::access(path.c_str(), F_OK) == 0;
}

util::IpAddr ExtractIpFromGid(const union ibv_gid& gid) {
  util::ipv6_t in6;
  std::memcpy(&in6, gid.raw, sizeof(in6));
  if (IN6_IS_ADDR_V4MAPPED(&in6)) {
    util::ipv4_t in4;
    std::memcpy(&in4.s_addr, &in6.s6_addr[12], sizeof(in4.s_addr));
    return util::IpAddr(in4);
  }
  return util::IpAddr(in6);
}

std::vector<TcpNic> DiscoverTcpNics(bool allow_loopback,
                                    absl::Span<const RdmaNic> rdma_nics) {
  std::vector<TcpNic> tcp_nics;
  struct ifaddrs* ifap = nullptr;
  if (::getifaddrs(&ifap) != 0 || ifap == nullptr) return tcp_nics;
  absl::Cleanup free_ifap = [ifap] { ::freeifaddrs(ifap); };

  for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_name == nullptr || ifa->ifa_addr == nullptr) continue;
    if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_RUNNING) == 0) {
      continue;
    }
    if ((ifa->ifa_flags & IFF_POINTOPOINT) != 0) continue;

    const bool is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
    if (is_loopback && !allow_loopback) continue;

    const std::string if_name(ifa->ifa_name);
    if (!is_loopback && IsRdmaNetdev(if_name)) continue;

    const std::optional<util::IpAddr> ip = util::IpAddr::Create(*ifa->ifa_addr);
    if (!ip.has_value() || !IsUsableIp(*ip, allow_loopback)) continue;

    PciLocation loc;
    if (is_loopback) {
      loc.numa_node = -1;
    } else {
      auto resolved = ResolveNetdevPciLocation(if_name);
      if (!resolved.ok()) {
        VLOG(1) << "Skipping " << if_name << ": " << resolved.status();
        continue;
      }
      loc = std::move(*resolved);
      if (std::any_of(rdma_nics.begin(), rdma_nics.end(),
                      [&](const RdmaNic& r) {
                        return r.netdev_name == if_name ||
                               (!loc.endpoint_bdf.empty() &&
                                r.pci.endpoint_bdf == loc.endpoint_bdf);
                      })) {
        continue;
      }
    }

    // Group multiple IPs/aliases on the same physical NIC into one TcpNic.
    auto it = std::find_if(
        tcp_nics.begin(), tcp_nics.end(), [&](const TcpNic& existing) {
          return !loc.endpoint_bdf.empty()
                     ? existing.pci.endpoint_bdf == loc.endpoint_bdf
                     : existing.if_name == if_name;
        });
    if (it != tcp_nics.end()) {
      if (std::find(it->ip_addrs.begin(), it->ip_addrs.end(), *ip) ==
          it->ip_addrs.end()) {
        it->ip_addrs.push_back(*ip);
      }
      continue;
    }

    tcp_nics.push_back(TcpNic{
        .pci = std::move(loc),
        .if_name = if_name,
        .ip_addrs = {*ip},
    });
  }
  return tcp_nics;
}

std::vector<RdmaNic> DiscoverRdmaNics(bool allow_loopback) {
  std::vector<RdmaNic> rdma_nics;
  int num_ib_devs = 0;
  struct ibv_device** dev_list = ::ibv_get_device_list(&num_ib_devs);
  if (dev_list == nullptr) return rdma_nics;
  absl::Cleanup free_ib = [dev_list] { ::ibv_free_device_list(dev_list); };

  for (int d = 0; d < num_ib_devs; ++d) {
    struct ibv_device* ib_dev = dev_list[d];
    if (ib_dev == nullptr) continue;
    const std::string ib_name(ib_dev->name);

    auto ib_loc = ResolveIbdevPciLocation(ib_name);
    if (!ib_loc.ok()) {
      VLOG(1) << "Skipping RDMA device " << ib_name << ": " << ib_loc.status();
      continue;
    }

    struct ibv_context* ctx = ::ibv_open_device(ib_dev);
    if (ctx == nullptr) continue;
    absl::Cleanup close_ctx = [ctx] { ::ibv_close_device(ctx); };

    struct ibv_device_attr dev_attr = {};
    if (::ibv_query_device(ctx, &dev_attr) != 0) continue;

    for (uint8_t port_num = 1; port_num <= dev_attr.phys_port_cnt; ++port_num) {
      struct ibv_port_attr port_attr = {};
      if (::ibv_query_port(ctx, port_num, &port_attr) != 0) continue;
      if (port_attr.state != IBV_PORT_ACTIVE) continue;

      int best_gid_idx = -1;
      union ibv_gid best_gid = {};
      std::string best_netdev;
      util::IpAddr best_ip;

      for (int i = 0; i < port_attr.gid_tbl_len; ++i) {
        struct ibv_gid_entry entry = {};
        if (::ibv_query_gid_ex(ctx, port_num, i, &entry, 0) != 0) continue;
        if (entry.gid_type != IBV_GID_TYPE_ROCE_V2) continue;

        const util::IpAddr under_ip = ExtractIpFromGid(entry.gid);
        if (!IsUsableIp(under_ip, allow_loopback)) continue;

        // Note: `if_indextoname` resolves the OS netdev for `ndev_ifindex`; if
        // a driver ever leaves stale GID entries for admin-DOWN subinterfaces,
        // we can also verify `IFF_UP` on the netdev here.
        char if_buf[IF_NAMESIZE] = {};
        if (::if_indextoname(entry.ndev_ifindex, if_buf) == nullptr) continue;

        // Prefer IPv4-mapped RoCEv2 GID first, or keep first valid IPv6.
        if (best_gid_idx == -1 || under_ip.IsIPv4()) {
          best_gid_idx = i;
          best_gid = entry.gid;
          best_netdev = if_buf;
          best_ip = under_ip;
          if (under_ip.IsIPv4()) break;
        }
      }

      if (best_gid_idx < 0) continue;

      util::ipv6_t gid_in6;
      std::memcpy(&gid_in6, best_gid.raw, sizeof(gid_in6));
      rdma_nics.push_back(RdmaNic{
          .pci = *ib_loc,
          .ib_dev_name = ib_name,
          .netdev_name = std::move(best_netdev),
          .port_num = port_num,
          .gid_index = best_gid_idx,
          .gid_addr = util::IpAddr(gid_in6),
          .ip_addr = best_ip,
      });
    }
  }
  return rdma_nics;
}

}  // namespace

NicInventory::NicInventory(std::vector<TcpNic> tcp_nics,
                           std::vector<RdmaNic> rdma_nics)
    : tcp_nics_(std::move(tcp_nics)), rdma_nics_(std::move(rdma_nics)) {
  sortAndAssignRails();
}

const NicInventory& NicInventory::Instance() {
  static const absl::NoDestructor<NicInventory> kInstance(
      NicInventory::Discover());
  return *kInstance;
}

NicInventory NicInventory::Discover(bool allow_loopback) {
  std::vector<RdmaNic> rdma_nics = DiscoverRdmaNics(allow_loopback);
  std::vector<TcpNic> tcp_nics = DiscoverTcpNics(allow_loopback, rdma_nics);
  return NicInventory(std::move(tcp_nics), std::move(rdma_nics));
}

void NicInventory::sortAndAssignRails() {
  std::sort(tcp_nics_.begin(), tcp_nics_.end(),
            [](const TcpNic& a, const TcpNic& b) {
              const uint64_t key_a = GetSortKey(a.pci);
              const uint64_t key_b = GetSortKey(b.pci);
              if (key_a != key_b) return key_a < key_b;
              return a.if_name < b.if_name;
            });
  for (size_t i = 0; i < tcp_nics_.size(); ++i) {
    tcp_nics_[i].rail_id = static_cast<uint32_t>(i);
  }

  std::sort(rdma_nics_.begin(), rdma_nics_.end(),
            [](const RdmaNic& a, const RdmaNic& b) {
              const uint64_t key_a = GetSortKey(a.pci);
              const uint64_t key_b = GetSortKey(b.pci);
              if (key_a != key_b) return key_a < key_b;
              if (a.ib_dev_name != b.ib_dev_name) {
                return a.ib_dev_name < b.ib_dev_name;
              }
              return a.port_num < b.port_num;
            });
  for (size_t i = 0; i < rdma_nics_.size(); ++i) {
    rdma_nics_[i].rail_id = static_cast<uint32_t>(i);
  }
}

}  // namespace peregrine::internal::nic
