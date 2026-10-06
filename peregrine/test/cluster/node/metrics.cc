#include "peregrine/test/cluster/node/metrics.h"

#include <sys/resource.h>

#include <algorithm>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/api/transport_metrics.h"

namespace peregrine::cluster {
namespace {

void MergeHistogram(Log2Histogram<32>& dst, const Log2Histogram<32>& src) {
  dst.sum += src.sum;
  for (int i = 0; i < dst.NumBuckets(); ++i) {
    dst.buckets[i] += src.buckets[i];
  }
}

void MergeOpMetrics(OpMetrics& dst, const OpMetrics& src) {
  MergeHistogram(dst.e2e_latency_us, src.e2e_latency_us);
  MergeHistogram(dst.request_size_bytes, src.request_size_bytes);
  dst.bytes += src.bytes;
  dst.errors += src.errors;
}

}  // namespace

CpuTimer::CpuTimer() { Reset(); }

void CpuTimer::Reset() {
  start_wall_ = absl::Now();
  struct rusage start_rusage{};  // NOLINT(misc-include-cleaner)
  getrusage(RUSAGE_SELF, &start_rusage);
  start_user_cpu_ = absl::DurationFromTimeval(start_rusage.ru_utime);
  start_sys_cpu_ = absl::DurationFromTimeval(start_rusage.ru_stime);
}

CpuStats CpuTimer::Snapshot() const {
  const absl::Time now_wall = absl::Now();
  struct rusage now_rusage{};  // NOLINT(misc-include-cleaner)
  getrusage(RUSAGE_SELF, &now_rusage);

  const absl::Duration user_delta =
      absl::DurationFromTimeval(now_rusage.ru_utime) - start_user_cpu_;
  const absl::Duration sys_delta =
      absl::DurationFromTimeval(now_rusage.ru_stime) - start_sys_cpu_;

  CpuStats stats;
  stats.wall_ms = absl::ToDoubleMilliseconds(
      std::max(absl::ZeroDuration(), now_wall - start_wall_));
  stats.user_cpu_ms = std::max(0.0, absl::ToDoubleMilliseconds(user_delta));
  stats.sys_cpu_ms = std::max(0.0, absl::ToDoubleMilliseconds(sys_delta));
  if (stats.wall_ms > 0.0) {
    stats.avg_cores = (stats.user_cpu_ms + stats.sys_cpu_ms) / stats.wall_ms;
  }
  return stats;
}

void MergeTransportMetrics(TransportMetrics& dst, const TransportMetrics& src) {
  MergeOpMetrics(dst.write, src.write);
  MergeOpMetrics(dst.read, src.read);
  dst.tcp_connect_failures += src.tcp_connect_failures;
  dst.rpc_requests_received += src.rpc_requests_received;
}

}  // namespace peregrine::cluster
