#ifndef PEREGRINE_SRC_INTERNAL_NIC_PCI_TOPOLOGY_H_
#define PEREGRINE_SRC_INTERNAL_NIC_PCI_TOPOLOGY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace peregrine::internal::nic {

// Hardware topology location of a PCIe device:
//   [NUMA Node] -> [Root Complex] -> [PCIe Switches...] -> [Endpoint BDF]
struct PciLocation {
  int32_t numa_node = 0;              // Local CPU/memory domain (e.g. 0, 1)
  std::string root_complex;           // CPU Root Complex (e.g. "pci0000:10")
  std::vector<std::string> switches;  // Intermediate PCIe switches (top-down)
  std::string endpoint_bdf;           // Endpoint BDF (e.g. "0000:11:00.0")
};

// Topological closeness between two PCIe devices.
enum class PciRelation : uint8_t {
  kSameEndpoint = 0,     // Same PCIe endpoint (0 hops)
  kSamePciSwitch = 1,    // Share a PCIe switch under the same Root Complex
  kSameRootComplex = 2,  // Share a Root Complex, but on different switches
  kSameNumaNode = 3,     // Same NUMA node, different Root Complexes
  kCrossNuma = 4,        // Different NUMA nodes
};

struct PciDistance {
  PciRelation relation = PciRelation::kCrossNuma;
  int hops = -1;  // Tree edges between devices (-1 if not same Root Complex)
};

// Computes the topological relation and PCIe hop count between two devices.
PciDistance GetPciDistance(const PciLocation& a, const PciLocation& b);

// Parses a PCIe BDF string ("XXXX:XX:XX.X") into a comparable 64-bit integer.
std::optional<uint64_t> ParsePciBdfKey(std::string_view bdf);

// Resolves a network interface name to its physical PCIe topology location.
absl::StatusOr<PciLocation> ResolveNetdevPciLocation(
    std::string_view if_name, std::string_view sysfs_root = "/");

// Resolves an InfiniBand/RDMA device name to its physical PCIe topology
// location.
absl::StatusOr<PciLocation> ResolveIbdevPciLocation(
    std::string_view ib_name, std::string_view sysfs_root = "/");

}  // namespace peregrine::internal::nic

#endif  // PEREGRINE_SRC_INTERNAL_NIC_PCI_TOPOLOGY_H_
