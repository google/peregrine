#include "peregrine/src/internal/engine2/connection_planner.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;

NicInfo IpListener(const char* name, std::vector<Endpoint> endpoints) {
  NicInfo info;
  info.name = name;
  info.type = util::NicType::kIP;
  info.endpoints = std::move(endpoints);
  return info;
}

TEST(ConnectionPlannerTest, RailIdForInterfaceHandlesRailedAndUnrailed) {
  HostInfo unrailed;
  EXPECT_THAT(RailIdForInterface(unrailed, "eth0"), Optional(kNoRail));

  HostInfo railed;
  railed.tcp_nics = {
      {.rail_id = 2, .if_name = "eth0"},
      {.rail_id = 4, .if_name = "eth1"},
      {.rail_id = std::numeric_limits<uint32_t>::max(), .if_name = "eth_bad"},
  };
  EXPECT_THAT(RailIdForInterface(railed, "eth0"), Optional(2));
  EXPECT_THAT(RailIdForInterface(railed, "eth1"), Optional(4));
  EXPECT_EQ(RailIdForInterface(railed, "eth2"), std::nullopt);
  EXPECT_EQ(RailIdForInterface(railed, "eth_bad"), std::nullopt);
}

TEST(ConnectionPlannerTest, ActiveRailIdsDefaultsToNoRailWhenUnrailed) {
  HostInfo host;
  host.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
  };
  EXPECT_THAT(ActiveRailIds(host), ElementsAre(kNoRail));
}

TEST(ConnectionPlannerTest, ActiveRailIdsReturnsOnlyActiveIpRails) {
  HostInfo host;
  host.tcp_nics = {
      {.rail_id = 3, .if_name = "eth0"},
      {.rail_id = 5, .if_name = "eth1"},
      {.rail_id = 7, .if_name = "eth2"},
  };
  NicInfo rdma;
  rdma.name = "eth2";
  rdma.type = util::NicType::kRDMA;
  host.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {}),
      rdma,
  };
  EXPECT_THAT(ActiveRailIds(host), ElementsAre(3));
}

TEST(ConnectionPlannerTest, PlanRailAlignedResolvesRailsAndEndpoints) {
  HostInfo self;
  self.control_plane_listener = Endpoint::Create("10.0.0.1:1000");
  self.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 1, .if_name = "eth1"},
  };
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {Endpoint::Create("[::1]:5001")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.tcp_nics = self.tcp_nics;
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.1.1:6000")}),
      IpListener("eth1", {Endpoint::Create("[::1]:6001")}),
  };

  Config config;
  config.num_conns_per_peer = 4;
  const ConnectionSpec spec0{
      .peer = peer.control_plane_listener,
      .local_rail_id = 0,
      .remote_rail_id = 0,
      .local_bind = Endpoint::Create("10.0.0.1:0"),
      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
  };
  const ConnectionSpec spec1{
      .peer = peer.control_plane_listener,
      .local_rail_id = 1,
      .remote_rail_id = 1,
      .local_bind = Endpoint::Create("[::1]:0"),
      .remote_ep = Endpoint::Create("[::1]:6001"),
  };
  EXPECT_THAT(PlanConnections(config, self, peer),
              ElementsAre(spec0, spec1, spec0, spec1));
}

TEST(ConnectionPlannerTest, PlanReturnsEmptyWhenNoActiveListeners) {
  HostInfo self;
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
  };
  HostInfo empty_peer;
  Config config;
  config.num_conns_per_peer = 2;
  EXPECT_THAT(PlanConnections(config, self, empty_peer), IsEmpty());
  EXPECT_THAT(PlanConnections(config, empty_peer, self), IsEmpty());
}

TEST(ConnectionPlannerTest, PlanRailAlignedFillsLeastLoadedRailsFirst) {
  HostInfo self;
  self.control_plane_listener = Endpoint::Create("10.0.0.1:1000");
  self.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 1, .if_name = "eth1"},
  };
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {Endpoint::Create("10.0.0.2:5001")}),
  };
  HostInfo peer = self;

  Config config;
  config.num_conns_per_peer = 4;
  const ConnectionSpec spec0{
      .peer = peer.control_plane_listener,
      .local_rail_id = 0,
      .remote_rail_id = 0,
      .local_bind = Endpoint::Create("10.0.0.1:0"),
      .remote_ep = Endpoint::Create("10.0.0.1:5000"),
  };
  const ConnectionSpec spec1{
      .peer = peer.control_plane_listener,
      .local_rail_id = 1,
      .remote_rail_id = 1,
      .local_bind = Endpoint::Create("10.0.0.2:0"),
      .remote_ep = Endpoint::Create("10.0.0.2:5001"),
  };

  // Two existing connections on rail 0 -> fill the two missing on rail 1.
  const std::vector<ConnectionSpec> skewed = {spec0, spec0};
  EXPECT_THAT(PlanConnections(config, self, peer, skewed),
              ElementsAre(spec1, spec1));

  // Unexpected cross-rail existing connection still caps total at 4.
  const std::vector<ConnectionSpec> cross_rail = {
      ConnectionSpec{.local_rail_id = 0, .remote_rail_id = 1}};
  EXPECT_THAT(PlanConnections(config, self, peer, cross_rail),
              ElementsAre(spec0, spec1, spec0));

  // Already at or above desired count -> no new connections.
  const std::vector<ConnectionSpec> full = {spec0, spec1, spec0, spec1};
  EXPECT_THAT(PlanConnections(config, self, peer, full), IsEmpty());
}

TEST(ConnectionPlannerTest, PlanAlignsRailsRegardlessOfListenerOrder) {
  HostInfo self;
  self.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 1, .if_name = "eth1"},
  };
  // Intentionally reversed order on `self`.
  self.data_plane_listeners = {
      IpListener("eth1", {Endpoint::Create("10.0.0.2:5001")}),
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.tcp_nics = self.tcp_nics;
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.1.1:6000")}),
      IpListener("eth1", {Endpoint::Create("10.0.1.2:6001")}),
  };

  Config config;
  config.num_conns_per_peer = 2;
  EXPECT_THAT(PlanConnections(config, self, peer),
              ElementsAre(
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 0,
                      .remote_rail_id = 0,
                      .local_bind = Endpoint::Create("10.0.0.1:0"),
                      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
                  },
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 1,
                      .remote_rail_id = 1,
                      .local_bind = Endpoint::Create("10.0.0.2:0"),
                      .remote_ep = Endpoint::Create("10.0.1.2:6001"),
                  }));
}

TEST(ConnectionPlannerTest, PlanAlignsCommonRailsWhenOneHostMissingRail) {
  HostInfo self;
  self.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 1, .if_name = "eth1"},
      {.rail_id = 2, .if_name = "eth2"},
  };
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {Endpoint::Create("10.0.0.2:5001")}),
      IpListener("eth2", {Endpoint::Create("10.0.0.3:5002")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 2, .if_name = "eth2"},
  };
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.1.1:6000")}),
      IpListener("eth2", {Endpoint::Create("10.0.1.3:6002")}),
  };

  Config config;
  config.num_conns_per_peer = 2;
  EXPECT_THAT(PlanConnections(config, self, peer),
              ElementsAre(
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 0,
                      .remote_rail_id = 0,
                      .local_bind = Endpoint::Create("10.0.0.1:0"),
                      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
                  },
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 2,
                      .remote_rail_id = 2,
                      .local_bind = Endpoint::Create("10.0.0.3:0"),
                      .remote_ep = Endpoint::Create("10.0.1.3:6002"),
                  }));
}

TEST(ConnectionPlannerTest, PlanDefaultUnrailedSelectsCompatibleFamilyUnbound) {
  HostInfo self;
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("[::1]:6001"),
                          Endpoint::Create("10.0.1.1:6000")}),
  };

  Config config;
  config.num_conns_per_peer = 1;
  EXPECT_THAT(PlanConnections(config, self, peer),
              ElementsAre(ConnectionSpec{
                  .peer = peer.control_plane_listener,
                  .local_rail_id = kNoRail,
                  .remote_rail_id = kNoRail,
                  .local_bind = Endpoint(),
                  .remote_ep = Endpoint::Create("10.0.1.1:6000"),
              }));
}

TEST(ConnectionPlannerTest, PlanDefaultUnrailedSpreadsAcrossRemoteListeners) {
  HostInfo self;
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {Endpoint::Create("10.0.0.2:5001")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.1.1:6000")}),
      IpListener("eth1", {Endpoint::Create("10.0.1.2:6001")}),
  };

  const ConnectionSpec spec0{
      .peer = peer.control_plane_listener,
      .local_rail_id = kNoRail,
      .remote_rail_id = kNoRail,
      .local_bind = Endpoint(),
      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
  };
  const ConnectionSpec spec1{
      .peer = peer.control_plane_listener,
      .local_rail_id = kNoRail,
      .remote_rail_id = kNoRail,
      .local_bind = Endpoint(),
      .remote_ep = Endpoint::Create("10.0.1.2:6001"),
  };

  Config config;
  config.num_conns_per_peer = 2;
  EXPECT_THAT(PlanConnections(config, self, peer),
              UnorderedElementsAre(spec0, spec1));
  EXPECT_THAT(PlanConnections(config, self, peer, {spec0}),
              ElementsAre(AnyOf(spec0, spec1)));
}

TEST(ConnectionPlannerTest, PlanDefaultRailedLocalExploresCartesianPairs) {
  HostInfo self;
  self.tcp_nics = {
      {.rail_id = 0, .if_name = "eth0"},
      {.rail_id = 1, .if_name = "eth1"},
  };
  self.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.0.1:5000")}),
      IpListener("eth1", {Endpoint::Create("10.0.0.2:5001")}),
  };
  HostInfo peer;
  peer.control_plane_listener = Endpoint::Create("10.0.1.1:2000");
  peer.data_plane_listeners = {
      IpListener("eth0", {Endpoint::Create("10.0.1.1:6000")}),
      IpListener("eth1", {Endpoint::Create("10.0.1.2:6001")}),
  };

  Config config;
  config.num_conns_per_peer = 4;
  EXPECT_THAT(PlanConnections(config, self, peer),
              UnorderedElementsAre(
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 0,
                      .remote_rail_id = kNoRail,
                      .local_bind = Endpoint::Create("10.0.0.1:0"),
                      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
                  },
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 1,
                      .remote_rail_id = kNoRail,
                      .local_bind = Endpoint::Create("10.0.0.2:0"),
                      .remote_ep = Endpoint::Create("10.0.1.2:6001"),
                  },
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 0,
                      .remote_rail_id = kNoRail,
                      .local_bind = Endpoint::Create("10.0.0.1:0"),
                      .remote_ep = Endpoint::Create("10.0.1.2:6001"),
                  },
                  ConnectionSpec{
                      .peer = peer.control_plane_listener,
                      .local_rail_id = 1,
                      .remote_rail_id = kNoRail,
                      .local_bind = Endpoint::Create("10.0.0.2:0"),
                      .remote_ep = Endpoint::Create("10.0.1.1:6000"),
                  }));
}

}  // namespace
}  // namespace peregrine::internal::engine2
