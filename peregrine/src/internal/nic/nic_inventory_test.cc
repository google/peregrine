#include "peregrine/src/internal/nic/nic_inventory.h"

#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal::nic {
namespace {

TEST(NicInventoryTest, SortsByPciBdfAndAssignsRails) {
  auto make_tcp = [](std::string_view bdf, int numa, std::string_view if_name,
                     std::vector<std::string_view> ips) {
    TcpNic tcp;
    tcp.pci.numa_node = numa;
    tcp.pci.root_complex = numa == 0 ? "pci0000:10" : "pci0000:80";
    tcp.pci.endpoint_bdf = std::string(bdf);
    tcp.if_name = std::string(if_name);
    for (std::string_view ip : ips) {
      tcp.ip_addrs.push_back(*util::IpAddr::Create(ip));
    }
    return tcp;
  };
  auto make_rdma = [](std::string_view bdf, int numa, std::string_view ib_name,
                      std::string_view if_name, std::string_view ip_str) {
    RdmaNic rdma;
    rdma.pci.numa_node = numa;
    rdma.pci.root_complex = numa == 0 ? "pci0000:10" : "pci0000:80";
    rdma.pci.endpoint_bdf = std::string(bdf);
    rdma.ib_dev_name = std::string(ib_name);
    rdma.netdev_name = std::string(if_name);
    rdma.port_num = 1;
    rdma.gid_index = 3;
    rdma.ip_addr = *util::IpAddr::Create(ip_str);
    rdma.gid_addr = rdma.ip_addr;
    return rdma;
  };

  // Pass NICs in reverse PCIe order, with 2 IPs on eth1; NicInventory must sort
  // them by BDF and assign rail_id 0 and 1.
  const NicInventory inv(
      {
          make_tcp("0000:86:00.0", 1, "eth2", {"10.0.2.1"}),
          make_tcp("0000:16:00.0", 0, "eth1", {"10.0.1.1", "10.0.1.101"}),
      },
      {
          make_rdma("0000:86:00.0", 1, "mlx5_1", "eth2", "10.0.2.1"),
          make_rdma("0000:16:00.0", 0, "mlx5_0", "eth1", "10.0.1.1"),
      });

  const auto tcp_nics = inv.GetTcpNics();
  ASSERT_EQ(tcp_nics.size(), 2u);
  EXPECT_EQ(tcp_nics[0].if_name, "eth1");
  EXPECT_EQ(tcp_nics[0].rail_id, 0u);
  EXPECT_EQ(tcp_nics[0].ip_addrs.size(), 2u);
  EXPECT_EQ(tcp_nics[1].if_name, "eth2");
  EXPECT_EQ(tcp_nics[1].rail_id, 1u);

  const auto rdma_nics = inv.GetRdmaNics();
  ASSERT_EQ(rdma_nics.size(), 2u);
  EXPECT_EQ(rdma_nics[0].ib_dev_name, "mlx5_0");
  EXPECT_EQ(rdma_nics[0].rail_id, 0u);
  EXPECT_EQ(rdma_nics[1].ib_dev_name, "mlx5_1");
  EXPECT_EQ(rdma_nics[1].rail_id, 1u);
}

TEST(NicInventoryTest, DiscoversLiveHostNics) {
  const auto inv = NicInventory::Discover(/*allow_loopback=*/false);
  for (const TcpNic& nic : inv.GetTcpNics()) {
    EXPECT_NE(nic.if_name, "lo");
    EXPECT_FALSE(nic.pci.endpoint_bdf.empty());
  }

  const auto inv_lo = NicInventory::Discover(/*allow_loopback=*/true);
  EXPECT_FALSE(inv_lo.GetTcpNics().empty());
}

}  // namespace
}  // namespace peregrine::internal::nic
