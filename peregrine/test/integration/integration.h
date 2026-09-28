#ifndef PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
#define PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/test/integration/controlpath-host.h"
#include "peregrine/test/integration/datapath-host.h"
#include "peregrine/test/integration/flags.h"
#include "peregrine/test/integration/settings.h"
#include "peregrine/test/workloads/workload_generator.h"

namespace peregrine::integration {

// Peregrine integration test harness.
class PeregrineIntegration final {
 public:
  struct Stats {
    int64_t transfers_completed = 0;
    int64_t bytes_transferred = 0;
    double throughput_gbps = 0.0;
  };

  PeregrineIntegration();

  void Run();
  void Stop() { stop_.store(true); }
  void CollectMetrics() const;

  Stats GetStats() const;
  const Settings& settings() const { return settings_; }
  const Flags& flags() const { return flags_; }

 private:
  bool shouldContinue() const;
  void sendRequest();
  void clearData(absl::Span<const Request> requests);
  void verifyData(absl::Span<const Request> requests) const;

 private:
  Settings settings_;
  Flags flags_;
  std::atomic<bool> stop_{false};

  std::unique_ptr<workloads::WorkloadGenerator> workload_;
  std::unique_ptr<ControlpathHost> controlpath_sndr_;
  std::unique_ptr<ControlpathHost> controlpath_rcvr_;
  std::unique_ptr<DatapathHost> datapath_sndr_;
  std::unique_ptr<DatapathHost> datapath_rcvr_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
