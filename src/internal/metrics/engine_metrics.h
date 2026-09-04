#ifndef PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_

#include <cstdint>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/lib/metric_counter.h"

namespace peregrine::internal {

struct EngineMetrics final {
  MetricCounter<uint64_t> tcp_connect_failures;

  void Snapshot(TransportMetrics& m) const {
    m.tcp_connect_failures = tcp_connect_failures.Value();
  }
};

static_assert(assumptions::kAddingNewMetricRules);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
