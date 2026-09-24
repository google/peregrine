#include "peregrine/src/api/transport_metrics.h"

#include "gtest/gtest.h"

namespace peregrine::testing {
namespace {

TEST(TransportMetricsTest, DefaultInitialization) {
  const TransportMetrics m;
  for (const OpMetrics* op : {&m.write, &m.read}) {
    EXPECT_EQ(op->e2e_latency_us.sum, 0);
    EXPECT_EQ(op->e2e_latency_us.Count(), 0);
    EXPECT_EQ(op->e2e_latency_us.NumBuckets(), 32);
    EXPECT_EQ(op->request_size_bytes.sum, 0);
    EXPECT_EQ(op->request_size_bytes.Count(), 0);
    EXPECT_EQ(op->request_size_bytes.NumBuckets(), 32);
    EXPECT_EQ(op->bytes, 0);
    EXPECT_EQ(op->errors, 0);
  }
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
