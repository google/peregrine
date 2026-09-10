#include "src/api/transport_metrics.h"

#include <cstddef>
#include <cstdint>

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportMetricsTest, DefaultInitialization) {
  const TransportMetrics m;

  // Verify every byte in the struct is zeroed without naming any fields.
  const auto* bytes = reinterpret_cast<const uint8_t*>(&m);
  for (size_t i = 0; i < sizeof(TransportMetrics); ++i) {
    EXPECT_EQ(bytes[i], 0);
  }
}

}  // namespace
}  // namespace peregrine::testing
