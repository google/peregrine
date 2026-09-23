#ifndef PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
#define PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/types/span.h"
#include "test/integration/controlpath-host.h"
#include "test/integration/datapath-host.h"
#include "test/integration/flags.h"
#include "test/integration/settings.h"
#include "test/integration/workloads/batch-generator.h"

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
  struct PendingHandle {
    Handle handle;
    int64_t bytes = 0;
  };
  using PendingHandles = std::vector<PendingHandle>;

  bool shouldContinue() const;
  void runBatch();
  PendingHandles postItems(absl::Span<PostItem> items);
  void pollHandles(PendingHandles& pending);
  void onTransferSuccess(int64_t bytes);

 private:
  Settings settings_;
  Flags flags_;
  std::atomic<bool> stop_{false};

  std::unique_ptr<ControlpathHost> controlpath_sndr_;
  std::unique_ptr<ControlpathHost> controlpath_rcvr_;
  std::unique_ptr<DatapathHost> datapath_sndr_;
  std::unique_ptr<DatapathHost> datapath_rcvr_;
  std::unique_ptr<BatchGenerator> batch_generator_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
