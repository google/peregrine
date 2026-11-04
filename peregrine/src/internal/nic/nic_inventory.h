#ifndef PEREGRINE_SRC_INTERNAL_NIC_NIC_INVENTORY_H_
#define PEREGRINE_SRC_INTERNAL_NIC_NIC_INVENTORY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "peregrine/src/internal/nic/pci_topology.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal::nic {

// Represents a discovered TCP/IP network interface and its usable IP addresses.
struct TcpNic {
  uint32_t rail_id = 0;                // Logical rail index
  PciLocation pci;                     // PCIe and NUMA topology location
  std::string if_name;                 // OS interface name (e.g. "eth0")
  std::vector<util::IpAddr> ip_addrs;  // Usable IP addresses on this interface
};

// Represents a discovered RDMA-capable network interface port.
struct RdmaNic {
  uint32_t rail_id = 0;     // Logical rail index
  PciLocation pci;          // PCIe and NUMA topology location
  std::string ib_dev_name;  // RDMA device name (e.g. "mlx5_0", "irdma0")
  std::string netdev_name;  // Associated OS network interface name
  uint8_t port_num = 1;     // 1-based physical port number
  int gid_index = 0;        // Selected RoCEv2 GID table index
  util::IpAddr gid_addr;    // 16-byte GID address
  util::IpAddr ip_addr;     // Associated IP address
};

// Inventory of discovered TCP and RDMA network interfaces on the host.
class NicInventory final {
 public:
  NicInventory() = default;
  NicInventory(std::vector<TcpNic> tcp_nics, std::vector<RdmaNic> rdma_nics);

  // Returns the process-wide default NIC inventory.
  static const NicInventory& Instance();

  // Discovers available TCP and RDMA interfaces on the host.
  static NicInventory Discover(bool allow_loopback = false);

  // Returns discovered non-RDMA TCP interfaces sorted by rail ID.
  absl::Span<const TcpNic> GetTcpNics() const { return tcp_nics_; }

  // Returns discovered RDMA interfaces sorted by rail ID.
  absl::Span<const RdmaNic> GetRdmaNics() const { return rdma_nics_; }

 private:
  // Sorts TCP and RDMA NICs by PCIe BDF and assigns 0..N-1 rail IDs.
  void sortAndAssignRails();

 private:
  std::vector<TcpNic> tcp_nics_;
  std::vector<RdmaNic> rdma_nics_;
};

}  // namespace peregrine::internal::nic

#endif  // PEREGRINE_SRC_INTERNAL_NIC_NIC_INVENTORY_H_
