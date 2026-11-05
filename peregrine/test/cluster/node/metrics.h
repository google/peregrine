#ifndef PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_
#define PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_

#include <cstdint>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::cluster {

// Snapshot of process wall time and CPU usage.
struct CpuStats {
  double wall_ms = 0.0;
  double user_cpu_ms = 0.0;
  double sys_cpu_ms = 0.0;
  double avg_cores = 0.0;
};

// Tracks wall clock time and process CPU consumption via
// getrusage(RUSAGE_SELF).
class CpuTimer {
 public:
  CpuTimer();

  void Reset();
  CpuStats Snapshot() const;

 private:
  absl::Time start_wall_;
  absl::Duration start_user_cpu_;
  absl::Duration start_sys_cpu_;
};

// Merges `src` TransportMetrics into `dst` (summing counters and histogram
// buckets across local Transport instances).
void MergeTransportMetrics(TransportMetrics& dst, const TransportMetrics& src);

// Complete node-level metrics snapshot (pure data struct, decoupled from wire
// formatting).
struct NodeMetricsSnapshot {
  int num_instances = 0;
  uint64_t xfer_size_bytes = 0;
  CpuStats cpu{};
  TransportMetrics transport{};
};

// Periodically captures and emits NodeMetricsSnapshot samples on a background
// thread while in scope; disabled when interval <= 0 or callbacks are null.
class ScopedMetricsSampler {
 public:
  using CaptureCallback = absl::AnyInvocable<NodeMetricsSnapshot()>;
  using SampleCallback = absl::AnyInvocable<void(const NodeMetricsSnapshot&)>;

  ScopedMetricsSampler(absl::Duration interval, CaptureCallback capture_fn,
                       SampleCallback on_sample);
  ~ScopedMetricsSampler();

  ScopedMetricsSampler(const ScopedMetricsSampler&) = delete;
  ScopedMetricsSampler& operator=(const ScopedMetricsSampler&) = delete;

  void Stop();

 private:
  absl::Duration interval_;
  CaptureCallback capture_fn_;
  SampleCallback on_sample_;
  absl::Notification stop_;
  util::Thread thread_;
};

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_
