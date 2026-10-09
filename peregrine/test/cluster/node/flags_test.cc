#include "peregrine/test/cluster/node/flags.h"

#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {
namespace {

TEST(FlagsTest, ValidateIpAllowsLoopbackAndRejectsWildcardOrUnknown) {
  auto v4_loop = ValidateIp("127.0.0.1");
  ASSERT_TRUE(v4_loop.ok());
  EXPECT_EQ(*v4_loop, "127.0.0.1");

  auto v6_loop = ValidateIp("::1");
  ASSERT_TRUE(v6_loop.ok());
  EXPECT_EQ(*v6_loop, "::1");

  EXPECT_FALSE(ValidateIp("").ok());
  EXPECT_FALSE(ValidateIp("0.0.0.0").ok());
  EXPECT_FALSE(ValidateIp("::").ok());
  EXPECT_FALSE(ValidateIp("203.0.113.254").ok());
}

TEST(FlagsTest, ReadNodeConfigParsesFlags) {
  absl::SetFlag(&FLAGS_ip, "127.0.0.1");
  absl::SetFlag(&FLAGS_num_instances, 16);
  absl::SetFlag(&FLAGS_node_index, 2);
  absl::SetFlag(&FLAGS_base_control_port, 20000);
  absl::SetFlag(&FLAGS_transport, "tcp");
  absl::SetFlag(&FLAGS_conn, 1);
  absl::SetFlag(&FLAGS_workload, "serial_fixed_write");
  absl::SetFlag(&FLAGS_targets, "127.0.0.1:10000@123456");
  absl::SetFlag(&FLAGS_traffic_pattern, "round_robin");
  absl::SetFlag(&FLAGS_num_xfers, 5);
  absl::SetFlag(&FLAGS_cpu_affinity, "numa:0");

  auto cfg = ReadNodeConfig();
  ASSERT_TRUE(cfg.ok()) << cfg.status();
  EXPECT_EQ(cfg->ip, "127.0.0.1");
  EXPECT_EQ(cfg->num_instances, 16);
  EXPECT_EQ(cfg->node_index, 2);
  EXPECT_EQ(cfg->base_control_port, 20000);
  EXPECT_EQ(cfg->traffic_pattern, TrafficPattern::kRoundRobin);
  EXPECT_EQ(cfg->num_xfers, 5);
  EXPECT_EQ(cfg->cpu_affinity, "numa:0");
  ASSERT_EQ(cfg->targets.size(), 1);
  EXPECT_EQ(cfg->targets[0].endpoint, "127.0.0.1:10000");
  EXPECT_EQ(cfg->targets[0].raddr, 123456);

  absl::SetFlag(&FLAGS_cpu_affinity, "invalid_affinity");
  EXPECT_FALSE(ReadNodeConfig().ok());
  absl::SetFlag(&FLAGS_cpu_affinity, "");
}

}  // namespace
}  // namespace peregrine::cluster
