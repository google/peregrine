#ifndef PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_
#define PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_

#include <cstdint>

#include "absl/time/time.h"
#include "peregrine/src/api/transport_metrics.h"

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

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_METRICS_H_
