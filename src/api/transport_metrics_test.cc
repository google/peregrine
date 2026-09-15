#include "src/api/transport_metrics.h"

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportMetricsTest, DefaultInitialization) {
  const TransportMetrics m;
  EXPECT_EQ(m.bytes_sent, 0);
  EXPECT_EQ(m.tcp_connect_failures, 0);
  EXPECT_EQ(m.rpc_requests_received, 0);
}

}  // namespace
}  // namespace peregrine::testing
