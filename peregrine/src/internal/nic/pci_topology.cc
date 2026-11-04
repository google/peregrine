#include "peregrine/src/internal/nic/pci_topology.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace peregrine::internal::nic {
namespace {

bool IsHexRange(std::string_view s) {
  if (s.empty()) return false;
  for (const char c : s) {
    if (!absl::ascii_isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool IsPciRootBusId(std::string_view name) {
  // Format: "pciXXXX:XX" (length 10).
  return name.size() == 10 && absl::StartsWith(name, "pci") && name[7] == ':' &&
         IsHexRange(name.substr(3, 4)) && IsHexRange(name.substr(8, 2));
}

bool PathExists(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0;
}

std::optional<std::string> RealPath(const std::string& path) {
  char resolved[PATH_MAX];
  if (::realpath(path.c_str(), resolved) == nullptr) {
    return std::nullopt;
  }
  return std::string(resolved);
}

std::string BaseName(std::string_view path) {
  while (!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  const size_t pos = path.rfind('/');
  return std::string(pos == std::string_view::npos ? path
                                                   : path.substr(pos + 1));
}

std::string ParentDir(std::string_view path) {
  while (!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  const size_t pos = path.rfind('/');
  if (pos == std::string_view::npos) return "";
  if (pos == 0) return "/";
  return std::string(path.substr(0, pos));
}

std::optional<std::string> ReadTrimmedFile(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return std::nullopt;
  char buf[256];
  const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
  ::close(fd);
  if (n < 0) return std::nullopt;
  buf[n] = '\0';
  return std::string(absl::StripAsciiWhitespace(buf));
}

int32_t ReadNumaNode(const std::string& pci_dev_dir) {
  const auto content = ReadTrimmedFile(absl::StrCat(pci_dev_dir, "/numa_node"));
  if (!content.has_value()) return 0;
  int32_t numa = 0;
  if (!absl::SimpleAtoi(*content, &numa) || numa < 0) {
    return 0;  // Normalize -1 (single-socket / unannotated ACPI) to NUMA 0.
  }
  return numa;
}

std::string JoinRoot(std::string_view sysfs_root, std::string_view rel_path) {
  while (!sysfs_root.empty() && sysfs_root.back() == '/') {
    sysfs_root.remove_suffix(1);
  }
  return absl::StrCat(sysfs_root, rel_path);
}

// Verifies that `dev_symlink_path` points to a physical PCIe, VirtIO-PCI, or
// auxiliary-bus device, and extracts its `PciLocation`.
absl::StatusOr<PciLocation> ResolveSingleDeviceLink(
    const std::string& dev_symlink_path) {
  const auto dev_real = RealPath(dev_symlink_path);
  if (!dev_real.has_value()) {
    return absl::NotFoundError(
        absl::StrCat("no hardware device link at ", dev_symlink_path));
  }

  const auto subsys_real = RealPath(absl::StrCat(*dev_real, "/subsystem"));
  if (!subsys_real.has_value()) {
    return absl::FailedPreconditionError(
        absl::StrCat("missing subsystem symlink for ", *dev_real));
  }

  std::string pci_dir = *dev_real;
  const std::string subsys = BaseName(*subsys_real);
  if (subsys == "virtio" || subsys == "auxiliary") {
    // VirtIO and Linux auxiliary-bus devices (e.g. idpf/irdma, mlx5 SF) live
    // one directory below their PCIe endpoint (e.g. ".../0000:00:04.0/virtio0"
    // or ".../0000:40:12.3/idpf.roce.0"). Walk up one level to the PCIe BDF.
    pci_dir = ParentDir(*dev_real);
    const auto parent_subsys = RealPath(absl::StrCat(pci_dir, "/subsystem"));
    if (!parent_subsys.has_value() || BaseName(*parent_subsys) != "pci") {
      return absl::FailedPreconditionError(
          absl::StrCat(subsys, " parent is not on pci bus: ", *dev_real));
    }
  } else if (subsys != "pci") {
    return absl::FailedPreconditionError(
        absl::StrCat("non-PCI subsystem '", subsys, "' at ", *dev_real));
  }

  // Parse the root-to-leaf chain starting from "pciXXXX:XX".
  std::string root_complex;
  std::vector<std::string> bdf_chain;
  for (const std::string_view part :
       absl::StrSplit(pci_dir, '/', absl::SkipEmpty())) {
    if (root_complex.empty()) {
      if (IsPciRootBusId(part)) {
        root_complex = std::string(part);
      }
      continue;
    }
    if (ParsePciBdfKey(part).has_value()) {
      bdf_chain.emplace_back(part);
    } else {
      break;
    }
  }

  if (root_complex.empty() || bdf_chain.empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("unable to parse PCIe hierarchy from ", pci_dir));
  }

  PciLocation loc;
  loc.numa_node = ReadNumaNode(pci_dir);
  loc.root_complex = std::move(root_complex);
  loc.endpoint_bdf = std::move(bdf_chain.back());
  bdf_chain.pop_back();
  loc.switches = std::move(bdf_chain);
  return loc;
}

}  // namespace

std::optional<uint64_t> ParsePciBdfKey(std::string_view bdf) {
  // Format: "XXXX:XX:XX.X" (length 12), e.g. "0000:86:00.0".
  if (bdf.size() != 12 || bdf[4] != ':' || bdf[7] != ':' || bdf[10] != '.') {
    return std::nullopt;
  }
  const std::string_view dom_s = bdf.substr(0, 4);
  const std::string_view bus_s = bdf.substr(5, 2);
  const std::string_view dev_s = bdf.substr(8, 2);
  const std::string_view fn_s = bdf.substr(11, 1);
  if (!IsHexRange(dom_s) || !IsHexRange(bus_s) || !IsHexRange(dev_s) ||
      !IsHexRange(fn_s)) {
    return std::nullopt;
  }
  uint32_t dom = 0, bus = 0, dev = 0, fn = 0;
  if (!absl::SimpleHexAtoi(dom_s, &dom) || !absl::SimpleHexAtoi(bus_s, &bus) ||
      !absl::SimpleHexAtoi(dev_s, &dev) || !absl::SimpleHexAtoi(fn_s, &fn)) {
    return std::nullopt;
  }
  return (static_cast<uint64_t>(dom) << 24) |
         (static_cast<uint64_t>(bus) << 16) |
         (static_cast<uint64_t>(dev) << 8) | static_cast<uint64_t>(fn);
}

PciDistance GetPciDistance(const PciLocation& a, const PciLocation& b) {
  if (!a.endpoint_bdf.empty() && a.endpoint_bdf == b.endpoint_bdf) {
    return PciDistance{.relation = PciRelation::kSameEndpoint, .hops = 0};
  }

  if (!a.root_complex.empty() && a.root_complex == b.root_complex) {
    size_t common_switches = 0;
    const size_t min_sw = std::min(a.switches.size(), b.switches.size());
    while (common_switches < min_sw &&
           a.switches[common_switches] == b.switches[common_switches]) {
      ++common_switches;
    }
    const int hops =
        static_cast<int>((a.switches.size() - common_switches + 1) +
                         (b.switches.size() - common_switches + 1));
    const PciRelation rel = (common_switches > 0)
                                ? PciRelation::kSamePciSwitch
                                : PciRelation::kSameRootComplex;
    return PciDistance{.relation = rel, .hops = hops};
  }

  if (a.numa_node >= 0 && a.numa_node == b.numa_node) {
    return PciDistance{.relation = PciRelation::kSameNumaNode, .hops = -1};
  }
  return PciDistance{.relation = PciRelation::kCrossNuma, .hops = -1};
}

absl::StatusOr<PciLocation> ResolveNetdevPciLocation(
    std::string_view if_name, std::string_view sysfs_root) {
  const std::string net_dir =
      JoinRoot(sysfs_root, absl::StrCat("/sys/class/net/", if_name));
  if (!PathExists(net_dir)) {
    return absl::NotFoundError(
        absl::StrCat("netdev sysfs entry not found: ", net_dir));
  }

  // 1. Reject software bridges (docker0, virbr0, cni0, etc.).
  if (PathExists(absl::StrCat(net_dir, "/bridge"))) {
    return absl::FailedPreconditionError(
        absl::StrCat(if_name, " is a software bridge"));
  }

  // 2. Direct physical / VirtIO PCIe device link.
  const std::string dev_link = absl::StrCat(net_dir, "/device");
  if (PathExists(dev_link)) {
    return ResolveSingleDeviceLink(dev_link);
  }

  // 3. Linux bonded interface: resolve physical slaves under /bonding or
  //    lower_*.
  if (PathExists(absl::StrCat(net_dir, "/bonding"))) {
    std::vector<std::string> slaves;
    if (const auto active =
            ReadTrimmedFile(absl::StrCat(net_dir, "/bonding/active_slave"));
        active.has_value() && !active->empty()) {
      slaves.push_back(*active);
    }
    if (const auto all_slaves =
            ReadTrimmedFile(absl::StrCat(net_dir, "/bonding/slaves"));
        all_slaves.has_value() && !all_slaves->empty()) {
      for (const std::string_view s :
           absl::StrSplit(*all_slaves, ' ', absl::SkipEmpty())) {
        const std::string s_str(s);
        if (std::find(slaves.begin(), slaves.end(), s_str) == slaves.end()) {
          slaves.push_back(s_str);
        }
      }
    }
    if (slaves.empty()) {
      if (DIR* dir = ::opendir(net_dir.c_str()); dir != nullptr) {
        while (struct dirent* ent = ::readdir(dir)) {
          const std::string_view dname(ent->d_name);
          if (absl::StartsWith(dname, "lower_")) {
            slaves.emplace_back(dname.substr(6));
          }
        }
        ::closedir(dir);
      }
    }

    for (const std::string& slave : slaves) {
      const std::string slave_dev_link = JoinRoot(
          sysfs_root, absl::StrCat("/sys/class/net/", slave, "/device"));
      if (auto s_loc = ResolveSingleDeviceLink(slave_dev_link); s_loc.ok()) {
        return s_loc;
      }
    }
    return absl::FailedPreconditionError(absl::StrCat(
        "bonded interface ", if_name, " has no physical PCIe slaves"));
  }

  return absl::FailedPreconditionError(absl::StrCat(
      if_name, " is a virtual interface with no physical PCIe backing"));
}

absl::StatusOr<PciLocation> ResolveIbdevPciLocation(
    std::string_view ib_name, std::string_view sysfs_root) {
  const std::string dev_link = JoinRoot(
      sysfs_root, absl::StrCat("/sys/class/infiniband/", ib_name, "/device"));
  return ResolveSingleDeviceLink(dev_link);
}

}  // namespace peregrine::internal::nic
