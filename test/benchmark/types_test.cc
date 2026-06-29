#include "test/benchmark/types.h"

#include "gtest/gtest.h"
#include "absl/time/time.h"

namespace peregrine::benchmark::testing {
namespace {

TEST(TypesTest, CalcRate) {
  EXPECT_EQ(CalcRate(1, absl::Microseconds(1)), Mbps(8));
  EXPECT_EQ(CalcRate(125, absl::Milliseconds(1)), Mbps(1));
  EXPECT_EQ(CalcRate(1'000, absl::Milliseconds(1)), Mbps(8));
  EXPECT_EQ(CalcRate(1024, absl::Microseconds(1)), Mbps(8192));
}

}  // namespace
}  // namespace peregrine::benchmark::testing
