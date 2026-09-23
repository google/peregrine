#include "src/api/transport_metrics.h"

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportMetricsTest, DefaultInitialization) {
  const TransportMetrics m;
  EXPECT_EQ(m.e2e_write_latency_us.sum, 0);
  EXPECT_EQ(m.e2e_write_latency_us.Count(), 0);
  EXPECT_EQ(m.e2e_write_latency_us.NumBuckets(), 32);
  EXPECT_EQ(m.bytes_sent, 0);
  EXPECT_EQ(m.request_write_size.sum, 0);
  EXPECT_EQ(m.request_write_size.Count(), 0);
  EXPECT_EQ(m.request_write_size.NumBuckets(), 32);
  EXPECT_EQ(m.request_read_size.sum, 0);
  EXPECT_EQ(m.request_read_size.Count(), 0);
  EXPECT_EQ(m.request_read_size.NumBuckets(), 32);
  EXPECT_EQ(m.e2e_write_errors, 0);
  EXPECT_EQ(m.tcp_connect_failures, 0);
  EXPECT_EQ(m.rpc_requests_received, 0);
}

TEST(TransportMetricsTest, Log2Histogram) {
  const Log2Histogram<4> h{.sum = 30, .buckets = {1, 2, 3, 0}};
  EXPECT_EQ(h.NumBuckets(), 4);
  EXPECT_EQ(h.Count(), 6);
  EXPECT_EQ(h.ToString(),
            "Log2Histogram(sum: 30, count: 6, buckets: [1, 2, 3, 0])");
}

}  // namespace
}  // namespace peregrine::testing
