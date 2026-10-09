#include "peregrine/test/cluster/node/affinity.h"

#include <sched.h>

#include <filesystem>  // NOLINT(build/c++17)
#include <fstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::cluster {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(AffinityTest, ParseCpuListHandlesSingleRangesAndDeduplication) {
  auto single = ParseCpuList("3");
  ASSERT_TRUE(single.ok()) << single.status();
  EXPECT_THAT(*single, ElementsAre(3));

  auto multi = ParseCpuList(" 0-2, 4, 120-121, 1 ");
  ASSERT_TRUE(multi.ok()) << multi.status();
  EXPECT_THAT(*multi, ElementsAre(0, 1, 2, 4, 120, 121));
}

TEST(AffinityTest, ParseCpuListRejectsInvalidSyntax) {
  EXPECT_FALSE(ParseCpuList("").ok());
  EXPECT_FALSE(ParseCpuList("   ").ok());
  EXPECT_FALSE(ParseCpuList("0,,1").ok());
  EXPECT_FALSE(ParseCpuList("0,").ok());
  EXPECT_FALSE(ParseCpuList("5-2").ok());
  EXPECT_FALSE(ParseCpuList("1-2-3").ok());
  EXPECT_FALSE(ParseCpuList("-1").ok());
  EXPECT_FALSE(ParseCpuList("abc").ok());
  EXPECT_FALSE(ParseCpuList(absl::StrCat(CPU_SETSIZE)).ok());
}

TEST(AffinityTest, ValidateCpuAffinitySpecNormalizesAndValidates) {
  auto empty = ValidateCpuAffinitySpec("");
  ASSERT_TRUE(empty.ok());
  EXPECT_EQ(*empty, "");

  auto none = ValidateCpuAffinitySpec("  NONE ");
  ASSERT_TRUE(none.ok());
  EXPECT_EQ(*none, "");

  auto numa = ValidateCpuAffinitySpec("NUMA:0");
  ASSERT_TRUE(numa.ok());
  EXPECT_EQ(*numa, "numa:0");

  auto list = ValidateCpuAffinitySpec("0-3,8");
  ASSERT_TRUE(list.ok());
  EXPECT_EQ(*list, "0-3,8");

  EXPECT_FALSE(ValidateCpuAffinitySpec("numa:").ok());
  EXPECT_FALSE(ValidateCpuAffinitySpec("numa:-1").ok());
  EXPECT_FALSE(ValidateCpuAffinitySpec("numa:abc").ok());
  EXPECT_FALSE(ValidateCpuAffinitySpec("auto").ok());
}

TEST(AffinityTest, ResolveCpuAffinitySpecReadsMockSysfsForNuma) {
  const std::filesystem::path tmp_root =
      std::filesystem::temp_directory_path() / "peregrine_affinity_test_sysfs";
  const std::filesystem::path node0_dir =
      tmp_root / "devices" / "system" / "node" / "node0";
  std::filesystem::create_directories(node0_dir);
  {
    std::ofstream out(node0_dir / "cpulist");
    out << "0-2,120-121\n";
  }

  auto none_res = ResolveCpuAffinitySpec("none", tmp_root.string());
  ASSERT_TRUE(none_res.ok()) << none_res.status();
  EXPECT_THAT(*none_res, IsEmpty());

  auto numa0_res = ResolveCpuAffinitySpec("numa:0", tmp_root.string());
  ASSERT_TRUE(numa0_res.ok()) << numa0_res.status();
  EXPECT_THAT(*numa0_res, ElementsAre(0, 1, 2, 120, 121));

  EXPECT_FALSE(ResolveCpuAffinitySpec("numa:99", tmp_root.string()).ok());
  std::filesystem::remove_all(tmp_root);
}

TEST(AffinityTest, ApplyCpuAffinityPinsCallingThreadToAllowedCpu) {
  util::Thread worker([]() {
    auto allowed_or = GetCurrentCpuAffinity();
    ASSERT_TRUE(allowed_or.ok()) << allowed_or.status();
    ASSERT_FALSE(allowed_or->empty());

    const int target_cpu = (*allowed_or)[0];
    ASSERT_TRUE(ApplyCpuAffinity(absl::StrCat(target_cpu)).ok());

    auto after_or = GetCurrentCpuAffinity();
    ASSERT_TRUE(after_or.ok()) << after_or.status();
    EXPECT_THAT(*after_or, ElementsAre(target_cpu));
  });
  worker.join();
}

TEST(AffinityTest, ApplyCpuAffinityRejectsNumaWithNoAllowedCpus) {
  const std::filesystem::path tmp_root =
      std::filesystem::temp_directory_path() /
      "peregrine_affinity_disjoint_test_sysfs";
  const std::filesystem::path node0_dir =
      tmp_root / "devices" / "system" / "node" / "node0";
  std::filesystem::create_directories(node0_dir);

  util::Thread worker([&]() {
    auto allowed_or = GetCurrentCpuAffinity();
    ASSERT_TRUE(allowed_or.ok()) << allowed_or.status();
    ASSERT_FALSE(allowed_or->empty());

    const int pinned_cpu = (*allowed_or)[0];
    ASSERT_TRUE(ApplyCpuAffinity(absl::StrCat(pinned_cpu)).ok());

    const int disallowed_cpu = (pinned_cpu + 1) % CPU_SETSIZE;
    {
      std::ofstream out(node0_dir / "cpulist");
      out << disallowed_cpu << "\n";
    }

    EXPECT_FALSE(ApplyCpuAffinity("numa:0", tmp_root.string()).ok());
  });
  worker.join();
  std::filesystem::remove_all(tmp_root);
}

}  // namespace
}  // namespace peregrine::cluster
