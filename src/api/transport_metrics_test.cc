#include "src/api/transport_metrics.h"

#include <cstddef>
#include <cstdint>

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportMetricsTest, DefaultInitialization) {
  const TransportMetrics m;
  (void)m;
}

TEST(TransportMetricsDetailsTest, DefaultInitialization) {
  const TransportMetricsDetails m;
  EXPECT_EQ(m.tcp_connect_failures, 0);
  EXPECT_EQ(m.rpc_requests_received, 0);
}

}  // namespace
}  // namespace peregrine::testing
