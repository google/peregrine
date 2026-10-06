#include "peregrine/test/cluster/node/topology.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(TopologyTest, ParseAndFormatTargets) {
  auto empty = ParseTargets("   ");
  ASSERT_TRUE(empty.ok());
  EXPECT_THAT(*empty, IsEmpty());

  auto parsed = ParseTargets("127.0.0.1:10000@140001, 10.0.0.2:10001@140002 ");
  ASSERT_TRUE(parsed.ok());
  EXPECT_THAT(
      *parsed,
      ElementsAre(PeerTarget{.endpoint = "127.0.0.1:10000", .raddr = 140001},
                  PeerTarget{.endpoint = "10.0.0.2:10001", .raddr = 140002}));

  EXPECT_EQ(FormatTargets(*parsed),
            "127.0.0.1:10000@140001,10.0.0.2:10001@140002");
}

TEST(TopologyTest, ParseTargetsRejectsInvalidInput) {
  EXPECT_FALSE(ParseTargets("127.0.0.1:10000").ok());
  EXPECT_FALSE(ParseTargets("127.0.0.1@12345").ok());
  EXPECT_FALSE(ParseTargets("127.0.0.1:10000@0").ok());
  EXPECT_FALSE(ParseTargets("127.0.0.1:10000@notanumber").ok());
}

TEST(TopologyTest, ParseTrafficPattern) {
  auto all_to_all = ParseTrafficPattern("all_to_all");
  ASSERT_TRUE(all_to_all.ok());
  EXPECT_EQ(*all_to_all, TrafficPattern::kAllToAll);
  EXPECT_EQ(TrafficPatternToString(*all_to_all), "all_to_all");

  auto rr = ParseTrafficPattern("ROUND_ROBIN");
  ASSERT_TRUE(rr.ok());
  EXPECT_EQ(*rr, TrafficPattern::kRoundRobin);
  EXPECT_EQ(TrafficPatternToString(*rr), "round_robin");

  EXPECT_FALSE(ParseTrafficPattern("one_to_one").ok());
}

TEST(TopologyTest, BuildTargetAssignmentsPassiveEmpty) {
  auto assignments = BuildTargetAssignments(
      /*node_index=*/0, /*num_local_instances=*/4, /*all_targets=*/{},
      TrafficPattern::kAllToAll);
  ASSERT_EQ(assignments.size(), 4);
  for (const auto& list : assignments) {
    EXPECT_THAT(list, IsEmpty());
  }
}

TEST(TopologyTest, BuildTargetAssignmentsFanInAndAllToAll) {
  std::vector<PeerTarget> targets = {
      {.endpoint = "127.0.0.1:10000", .raddr = 111},
      {.endpoint = "127.0.0.1:10001", .raddr = 222},
  };
  auto assignments = BuildTargetAssignments(
      /*node_index=*/0, /*num_local_instances=*/3, targets,
      TrafficPattern::kAllToAll);
  ASSERT_EQ(assignments.size(), 3);
  for (const auto& list : assignments) {
    EXPECT_THAT(list, ElementsAre(targets[0], targets[1]));
  }
}

TEST(TopologyTest, BuildTargetAssignmentsExcludeSelf) {
  std::vector<PeerTarget> targets = {
      {.endpoint = "127.0.0.1:10000", .raddr = 111},
      {.endpoint = "127.0.0.1:10001", .raddr = 222},
  };
  std::vector<std::string> local_eps = {"127.0.0.1:10000", "127.0.0.1:10001"};
  auto assignments = BuildTargetAssignments(
      /*node_index=*/0, /*num_local_instances=*/2, targets,
      TrafficPattern::kAllToAll, /*exclude_self=*/true, local_eps);
  ASSERT_EQ(assignments.size(), 2);
  EXPECT_THAT(assignments[0], ElementsAre(targets[1]));
  EXPECT_THAT(assignments[1], ElementsAre(targets[0]));
}

TEST(TopologyTest, BuildTargetAssignmentsRoundRobinAcrossNodes) {
  std::vector<PeerTarget> targets = {
      {.endpoint = "10.0.0.1:10000", .raddr = 1},
      {.endpoint = "10.0.0.1:10001", .raddr = 2},
      {.endpoint = "10.0.0.1:10002", .raddr = 3},
  };
  auto assignments = BuildTargetAssignments(
      /*node_index=*/1, /*num_local_instances=*/2, targets,
      TrafficPattern::kRoundRobin);
  ASSERT_EQ(assignments.size(), 2);
  EXPECT_THAT(assignments[0], ElementsAre(targets[2]));
  EXPECT_THAT(assignments[1], ElementsAre(targets[0]));
}

}  // namespace
}  // namespace peregrine::cluster
