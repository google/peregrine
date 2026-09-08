#include "test/benchmark/types.h"

#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/time/time.h"

namespace peregrine::benchmark::testing {
namespace {

TEST(TypesTest, CalcRate) {
  EXPECT_EQ(CalcRate(1, absl::Microseconds(1)), Mbps(8));
  EXPECT_EQ(CalcRate(125, absl::Milliseconds(1)), Mbps(1));
  EXPECT_EQ(CalcRate(1'000, absl::Milliseconds(1)), Mbps(8));
  EXPECT_EQ(CalcRate(1024, absl::Microseconds(1)), Mbps(8192));
}

TEST(TypesTest, WorkloadTypeToString) {
  EXPECT_EQ(ToString(WorkloadType::kSerialFixedWrite), "serial_fixed_write");
}

TEST(TypesTest, WorkloadTypeFlagParsing) {
  WorkloadType workload;
  std::string error;
  EXPECT_TRUE(AbslParseFlag("serial_fixed_write", &workload, &error));
  EXPECT_EQ(workload, WorkloadType::kSerialFixedWrite);
  EXPECT_TRUE(AbslParseFlag("SERIAL_FIXED_WRITE", &workload, &error));
  EXPECT_EQ(workload, WorkloadType::kSerialFixedWrite);

  EXPECT_FALSE(AbslParseFlag("invalid_workload", &workload, &error));
  EXPECT_TRUE(
      absl::StrContains(error, "Supported workloads: [serial_fixed_write]"));
}

}  // namespace
}  // namespace peregrine::benchmark::testing
