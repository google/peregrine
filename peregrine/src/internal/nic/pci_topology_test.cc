#include "peregrine/src/internal/nic/pci_topology.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace peregrine::internal::nic {
namespace {

using ::testing::ElementsAre;

void MkdirAll(std::string_view path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur.push_back(path[i]);
    if (path[i] == '/' || i + 1 == path.size()) {
      if (!cur.empty() && cur != "/") {
        ::mkdir(cur.c_str(), 0755);
      }
    }
  }
}

void WriteTextFile(std::string_view path, std::string_view content) {
  const size_t slash = path.rfind('/');
  if (slash != std::string_view::npos) {
    MkdirAll(path.substr(0, slash));
  }
  const std::string p(path);
  const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  ASSERT_GE(fd, 0) << "Failed to open " << p;
  ASSERT_EQ(::write(fd, content.data(), content.size()),
            static_cast<ssize_t>(content.size()));
  ::close(fd);
}

void MakeSymlink(std::string_view target, std::string_view link_path) {
  const size_t slash = link_path.rfind('/');
  if (slash != std::string_view::npos) {
    MkdirAll(link_path.substr(0, slash));
  }
  const std::string t(target);
  const std::string l(link_path);
  ::unlink(l.c_str());
  ASSERT_EQ(::symlink(t.c_str(), l.c_str()), 0)
      << "Failed symlink " << l << " -> " << t;
}

class SyntheticSysfsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = absl::StrCat(::testing::TempDir(), "/peregrine_sysfs_", ::getpid());
    MkdirAll(absl::StrCat(root_, "/sys/bus/pci"));
    MkdirAll(absl::StrCat(root_, "/sys/bus/virtio"));
    MkdirAll(absl::StrCat(root_, "/sys/bus/auxiliary"));
    MkdirAll(absl::StrCat(root_, "/sys/bus/usb"));
    MkdirAll(absl::StrCat(root_, "/sys/class/net"));
    MkdirAll(absl::StrCat(root_, "/sys/class/infiniband"));
  }

  void CreatePciNic(std::string_view pci_chain_rel, int numa_node,
                    std::string_view if_name, std::string_view ib_name = "") {
    const std::string dev_dir =
        absl::StrCat(root_, "/sys/devices/", pci_chain_rel);
    MkdirAll(dev_dir);
    WriteTextFile(absl::StrCat(dev_dir, "/numa_node"),
                  absl::StrCat(numa_node, "\n"));
    MakeSymlink(absl::StrCat(root_, "/sys/bus/pci"),
                absl::StrCat(dev_dir, "/subsystem"));

    if (!if_name.empty()) {
      const std::string net_dir = absl::StrCat(dev_dir, "/net/", if_name);
      MkdirAll(net_dir);
      MakeSymlink(dev_dir, absl::StrCat(net_dir, "/device"));
      MakeSymlink(net_dir, absl::StrCat(root_, "/sys/class/net/", if_name));
    }
    if (!ib_name.empty()) {
      const std::string ib_dir = absl::StrCat(dev_dir, "/infiniband/", ib_name);
      MkdirAll(ib_dir);
      MakeSymlink(dev_dir, absl::StrCat(ib_dir, "/device"));
      MakeSymlink(ib_dir,
                  absl::StrCat(root_, "/sys/class/infiniband/", ib_name));
    }
  }

  std::string root_;
};

TEST(PciTopologyTest, ParsesValidAndRejectsInvalidBdf) {
  const auto key1 = ParsePciBdfKey("0000:16:00.0");
  const auto key2 = ParsePciBdfKey("0000:86:00.0");
  const auto key3 = ParsePciBdfKey("0000:86:00.1");
  ASSERT_TRUE(key1.has_value());
  ASSERT_TRUE(key2.has_value());
  ASSERT_TRUE(key3.has_value());
  EXPECT_LT(*key1, *key2);
  EXPECT_LT(*key2, *key3);

  EXPECT_FALSE(ParsePciBdfKey("pci0000:80").has_value());
  EXPECT_FALSE(ParsePciBdfKey("virtio0").has_value());
  EXPECT_FALSE(ParsePciBdfKey("0000:86:00").has_value());
}

TEST_F(SyntheticSysfsTest, ResolvesPciLocationsAndComputesDistances) {
  // NUMA 0, Root Complex pci0000:10, Switch 0000:10:01.0 -> eth1 & eth2
  CreatePciNic("pci0000:10/0000:10:01.0/0000:11:00.0", 0, "eth1", "mlx5_0");
  CreatePciNic("pci0000:10/0000:10:01.0/0000:11:00.1", 0, "eth2", "mlx5_1");
  // NUMA 0, Root Complex pci0000:10, Switch 0000:10:02.0 -> eth3
  CreatePciNic("pci0000:10/0000:10:02.0/0000:12:00.0", 0, "eth3", "mlx5_2");
  // NUMA 0, Different Root Complex pci0000:20 -> eth4
  CreatePciNic("pci0000:20/0000:20:01.0/0000:21:00.0", 0, "eth4", "mlx5_3");
  // NUMA 1, Root Complex pci0000:80 -> eth5
  CreatePciNic("pci0000:80/0000:80:01.0/0000:81:00.0", 1, "eth5", "mlx5_4");

  const auto loc1 = ResolveNetdevPciLocation("eth1", root_);
  const auto loc2 = ResolveNetdevPciLocation("eth2", root_);
  const auto loc3 = ResolveNetdevPciLocation("eth3", root_);
  const auto loc4 = ResolveNetdevPciLocation("eth4", root_);
  const auto loc5 = ResolveNetdevPciLocation("eth5", root_);
  const auto ib1 = ResolveIbdevPciLocation("mlx5_0", root_);

  ASSERT_TRUE(loc1.ok()) << loc1.status();
  ASSERT_TRUE(loc2.ok()) << loc2.status();
  ASSERT_TRUE(loc3.ok()) << loc3.status();
  ASSERT_TRUE(loc4.ok()) << loc4.status();
  ASSERT_TRUE(loc5.ok()) << loc5.status();
  ASSERT_TRUE(ib1.ok()) << ib1.status();

  EXPECT_EQ(loc1->numa_node, 0);
  EXPECT_EQ(loc1->root_complex, "pci0000:10");
  EXPECT_THAT(loc1->switches, ElementsAre("0000:10:01.0"));
  EXPECT_EQ(loc1->endpoint_bdf, "0000:11:00.0");
  EXPECT_EQ(ib1->endpoint_bdf, "0000:11:00.0");

  const PciDistance d_same = GetPciDistance(*loc1, *ib1);
  EXPECT_EQ(d_same.relation, PciRelation::kSameEndpoint);
  EXPECT_EQ(d_same.hops, 0);

  const PciDistance d_switch = GetPciDistance(*loc1, *loc2);
  EXPECT_EQ(d_switch.relation, PciRelation::kSamePciSwitch);
  EXPECT_EQ(d_switch.hops, 2);

  const PciDistance d_rc = GetPciDistance(*loc1, *loc3);
  EXPECT_EQ(d_rc.relation, PciRelation::kSameRootComplex);
  EXPECT_EQ(d_rc.hops, 4);

  const PciDistance d_numa = GetPciDistance(*loc1, *loc4);
  EXPECT_EQ(d_numa.relation, PciRelation::kSameNumaNode);
  EXPECT_EQ(d_numa.hops, -1);

  const PciDistance d_sys = GetPciDistance(*loc1, *loc5);
  EXPECT_EQ(d_sys.relation, PciRelation::kCrossNuma);
  EXPECT_EQ(d_sys.hops, -1);
}

TEST_F(SyntheticSysfsTest, SupportsVirtioPciLinuxBondingAndAuxiliaryBus) {
  // 1. VirtIO-PCI NIC: .../pci0000:00/0000:00:04.0/virtio0/net/ens4
  const std::string pci_dir =
      absl::StrCat(root_, "/sys/devices/pci0000:00/0000:00:04.0");
  const std::string virtio_dir = absl::StrCat(pci_dir, "/virtio0");
  const std::string net_dir = absl::StrCat(virtio_dir, "/net/ens4");
  MkdirAll(net_dir);
  WriteTextFile(absl::StrCat(pci_dir, "/numa_node"), "-1\n");
  MakeSymlink(absl::StrCat(root_, "/sys/bus/pci"),
              absl::StrCat(pci_dir, "/subsystem"));
  MakeSymlink(absl::StrCat(root_, "/sys/bus/virtio"),
              absl::StrCat(virtio_dir, "/subsystem"));
  MakeSymlink(virtio_dir, absl::StrCat(net_dir, "/device"));
  MakeSymlink(net_dir, absl::StrCat(root_, "/sys/class/net/ens4"));

  const auto virtio_loc = ResolveNetdevPciLocation("ens4", root_);
  ASSERT_TRUE(virtio_loc.ok()) << virtio_loc.status();
  EXPECT_EQ(virtio_loc->root_complex, "pci0000:00");
  EXPECT_TRUE(virtio_loc->switches.empty());
  EXPECT_EQ(virtio_loc->endpoint_bdf, "0000:00:04.0");
  EXPECT_EQ(virtio_loc->numa_node, 0);

  // 2. Linux Bonded NIC: bond0 backed by eth1 and eth2
  CreatePciNic("pci0000:10/0000:10:01.0/0000:11:00.0", 0, "eth1");
  CreatePciNic("pci0000:10/0000:10:01.0/0000:11:00.1", 0, "eth2");
  const std::string bond_dir =
      absl::StrCat(root_, "/sys/devices/virtual/net/bond0");
  MkdirAll(absl::StrCat(bond_dir, "/bonding"));
  WriteTextFile(absl::StrCat(bond_dir, "/bonding/active_slave"), "eth1\n");
  WriteTextFile(absl::StrCat(bond_dir, "/bonding/slaves"), "eth1 eth2\n");
  MakeSymlink(bond_dir, absl::StrCat(root_, "/sys/class/net/bond0"));

  const auto bond_loc = ResolveNetdevPciLocation("bond0", root_);
  ASSERT_TRUE(bond_loc.ok()) << bond_loc.status();
  EXPECT_EQ(bond_loc->endpoint_bdf, "0000:11:00.0");

  // 3. Auxiliary-bus RDMA device (irdma0 under 0000:11:00.0/idpf.roce.0)
  const std::string aux_dir = absl::StrCat(
      root_, "/sys/devices/pci0000:10/0000:10:01.0/0000:11:00.0/idpf.roce.0");
  const std::string ib_dir = absl::StrCat(aux_dir, "/infiniband/irdma0");
  MkdirAll(ib_dir);
  MakeSymlink(absl::StrCat(root_, "/sys/bus/auxiliary"),
              absl::StrCat(aux_dir, "/subsystem"));
  MakeSymlink(aux_dir, absl::StrCat(ib_dir, "/device"));
  MakeSymlink(ib_dir, absl::StrCat(root_, "/sys/class/infiniband/irdma0"));

  const auto aux_loc = ResolveIbdevPciLocation("irdma0", root_);
  ASSERT_TRUE(aux_loc.ok()) << aux_loc.status();
  EXPECT_EQ(aux_loc->endpoint_bdf, "0000:11:00.0");
}

TEST_F(SyntheticSysfsTest, RejectsBridgeVirtualAndUsbBmcInterfaces) {
  // 1. Software bridge (docker0)
  const std::string bridge_dir =
      absl::StrCat(root_, "/sys/devices/virtual/net/docker0");
  MkdirAll(absl::StrCat(bridge_dir, "/bridge"));
  MakeSymlink(bridge_dir, absl::StrCat(root_, "/sys/class/net/docker0"));
  EXPECT_FALSE(ResolveNetdevPciLocation("docker0", root_).ok());

  // 2. Virtual veth (veth0)
  const std::string veth_dir =
      absl::StrCat(root_, "/sys/devices/virtual/net/veth0");
  MkdirAll(veth_dir);
  MakeSymlink(veth_dir, absl::StrCat(root_, "/sys/class/net/veth0"));
  EXPECT_FALSE(ResolveNetdevPciLocation("veth0", root_).ok());

  // 3. USB-to-BMC interface attached to PCIe xHCI host controller (usb0)
  const std::string usb_dev = absl::StrCat(
      root_, "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1:1.0");
  const std::string usb_net = absl::StrCat(usb_dev, "/net/usb0");
  MkdirAll(usb_net);
  MakeSymlink(absl::StrCat(root_, "/sys/bus/usb"),
              absl::StrCat(usb_dev, "/subsystem"));
  MakeSymlink(usb_dev, absl::StrCat(usb_net, "/device"));
  MakeSymlink(usb_net, absl::StrCat(root_, "/sys/class/net/usb0"));
  EXPECT_FALSE(ResolveNetdevPciLocation("usb0", root_).ok());
}

}  // namespace
}  // namespace peregrine::internal::nic
