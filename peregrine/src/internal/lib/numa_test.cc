#include "peregrine/src/internal/lib/numa.h"

#include <sched.h>

#include <algorithm>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

std::vector<int> CurrentThreadAllowedCpus() {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (::sched_getaffinity(0, sizeof(set), &set) != 0) {
    return {};
  }
  std::vector<int> cpus;
  for (int c = 0; c < CPU_SETSIZE; ++c) {
    if (CPU_ISSET(c, &set)) {
      cpus.push_back(c);
    }
  }
  return cpus;
}

TEST(NumaTest, RejectsNegativeAndMissingNumaNode) {
  EXPECT_THAT(GetNumaCpus(-1), IsEmpty());
  EXPECT_THAT(GetNumaCpus(999), IsEmpty());
}

TEST(NumaTest, DiscoversLiveHostNumaCpusWithinAllowedAffinity) {
  const std::vector<int> allowed = CurrentThreadAllowedCpus();
  ASSERT_FALSE(allowed.empty());

  std::vector<int> discovered;
  for (int node = 0; node < 64; ++node) {
    const std::vector<int> cpus = GetNumaCpus(node);
    EXPECT_TRUE(std::is_sorted(cpus.begin(), cpus.end()));
    for (int cpu : cpus) {
      discovered.push_back(cpu);
      EXPECT_TRUE(std::binary_search(allowed.begin(), allowed.end(), cpu))
          << "CPU " << cpu << " from node " << node
          << " is outside sched_getaffinity";
    }
  }
  EXPECT_FALSE(discovered.empty());
}

TEST(NumaTest, IntersectsNumaCpusWithCallingThreadAffinity) {
  int target_node = -1;
  int target_cpu = -1;
  for (int node = 0; node < 64; ++node) {
    const std::vector<int> cpus = GetNumaCpus(node);
    if (!cpus.empty()) {
      target_node = node;
      target_cpu = cpus.front();
      break;
    }
  }
  ASSERT_GE(target_node, 0);
  ASSERT_GE(target_cpu, 0);

  util::Thread worker([target_node, target_cpu]() {
    ASSERT_TRUE(PinCurrentThreadToCpu(target_cpu).ok());
    EXPECT_THAT(GetNumaCpus(target_node), ElementsAre(target_cpu));
    EXPECT_THAT(GetNumaCpus(target_node + 1), IsEmpty());
  });
  worker.join();
}

TEST(NumaTest, PinCurrentThreadNegativeCpuIsNoOp) {
  const std::vector<int> before = CurrentThreadAllowedCpus();
  EXPECT_TRUE(PinCurrentThreadToCpu(-1).ok());
  EXPECT_EQ(CurrentThreadAllowedCpus(), before);
}

TEST(NumaTest, PinWorkerThreadToAllowedCpusAndRejectInvalidCpu) {
  const std::vector<int> allowed = CurrentThreadAllowedCpus();
  ASSERT_FALSE(allowed.empty());

  util::Thread worker([&allowed]() {
    const int first_cpu = allowed.front();
    ASSERT_TRUE(PinCurrentThreadToCpu(first_cpu).ok());
    EXPECT_THAT(CurrentThreadAllowedCpus(), ElementsAre(first_cpu));
    EXPECT_EQ(::sched_getcpu(), first_cpu);

    if (allowed.size() >= 2) {
      const int last_cpu = allowed.back();
      ASSERT_TRUE(PinCurrentThreadToCpu(last_cpu).ok());
      EXPECT_THAT(CurrentThreadAllowedCpus(), ElementsAre(last_cpu));
      EXPECT_EQ(::sched_getcpu(), last_cpu);
    }

    const absl::Status bad = PinCurrentThreadToCpu(65535);
    EXPECT_FALSE(bad.ok());
  });
  worker.join();
}

}  // namespace
}  // namespace peregrine::internal
