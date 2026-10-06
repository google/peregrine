#include "peregrine/test/cluster/node/metrics.h"

#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/api/transport_metrics.h"

namespace peregrine::cluster {
namespace {

TEST(MetricsTest, MergeTransportMetricsAccumulatesAllFields) {
  TransportMetrics a;
  a.write.bytes = 100;
  a.write.errors = 1;
  a.write.e2e_latency_us.sum = 500;
  a.write.e2e_latency_us.buckets[3] = 2;
  a.tcp_connect_failures = 4;
  a.rpc_requests_received = 10;

  TransportMetrics b;
  b.write.bytes = 250;
  b.write.errors = 2;
  b.write.e2e_latency_us.sum = 700;
  b.write.e2e_latency_us.buckets[3] = 5;
  b.read.bytes = 64;
  b.tcp_connect_failures = 1;
  b.rpc_requests_received = 20;

  MergeTransportMetrics(a, b);
  EXPECT_EQ(a.write.bytes, 350);
  EXPECT_EQ(a.write.errors, 3);
  EXPECT_EQ(a.write.e2e_latency_us.sum, 1200);
  EXPECT_EQ(a.write.e2e_latency_us.buckets[3], 7);
  EXPECT_EQ(a.read.bytes, 64);
  EXPECT_EQ(a.tcp_connect_failures, 5);
  EXPECT_EQ(a.rpc_requests_received, 30);
}

TEST(MetricsTest, CpuTimerMeasuresElapsedTime) {
  CpuTimer timer;
  absl::SleepFor(absl::Milliseconds(10));
  CpuStats snap = timer.Snapshot();
  EXPECT_GE(snap.wall_ms, 5.0);
  EXPECT_GE(snap.user_cpu_ms, 0.0);
  EXPECT_GE(snap.sys_cpu_ms, 0.0);
}

}  // namespace
}  // namespace peregrine::cluster
