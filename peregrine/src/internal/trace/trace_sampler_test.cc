#include "peregrine/src/internal/trace/trace_sampler.h"

#include "gtest/gtest.h"

namespace peregrine::internal::testing {
namespace {

TEST(TraceSamplerTest, SamplingRates) {
  TraceSampler disabled;
  EXPECT_FALSE(disabled.IsEnabled());
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(disabled.ShouldSample());
  }

  TraceSampler always(1);
  EXPECT_TRUE(always.IsEnabled());
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(always.ShouldSample());
  }

  TraceSampler one_in_four(4);
  EXPECT_TRUE(one_in_four.IsEnabled());
  int sampled = 0;
  for (int i = 0; i < 16; ++i) {
    if (one_in_four.ShouldSample()) ++sampled;
  }
  EXPECT_EQ(sampled, 4);
}

}  // namespace
}  // namespace peregrine::internal::testing
