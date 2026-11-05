#include "peregrine/test/cluster/node/metrics.h"

#include <atomic>
#include <cstdint>

#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"
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

TEST(MetricsTest, ScopedMetricsSamplerEmitsUntilStopped) {
  std::atomic<uint64_t> captured{0};
  std::atomic<uint64_t> last_seen{0};
  absl::Notification sampled_twice;

  ScopedMetricsSampler sampler(
      absl::Milliseconds(5),
      [&] {
        return NodeMetricsSnapshot{
            .xfer_size_bytes = captured.fetch_add(1) + 1,
        };
      },
      [&](const NodeMetricsSnapshot& snap) {
        last_seen.store(snap.xfer_size_bytes);
        if (snap.xfer_size_bytes == 2) {
          sampled_twice.Notify();
        }
      });

  ASSERT_TRUE(sampled_twice.WaitForNotificationWithTimeout(absl::Seconds(5)));
  sampler.Stop();
  const uint64_t count_after_stop = captured.load();
  EXPECT_GE(last_seen.load(), 2);
  EXPECT_EQ(captured.load(), count_after_stop);
}

TEST(MetricsTest, ScopedMetricsSamplerDisabledWhenIntervalZeroOrCallbackNull) {
  std::atomic<int> captures{0};
  {
    ScopedMetricsSampler zero_interval(
        absl::ZeroDuration(),
        [&] {
          ++captures;
          return NodeMetricsSnapshot{};
        },
        [](const NodeMetricsSnapshot&) {});
    ScopedMetricsSampler null_callback(
        absl::Milliseconds(1),
        [&] {
          ++captures;
          return NodeMetricsSnapshot{};
        },
        nullptr);
  }
  EXPECT_EQ(captures.load(), 0);
}

}  // namespace
}  // namespace peregrine::cluster
