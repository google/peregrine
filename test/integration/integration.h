#ifndef PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
#define PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "test/integration/controlpath-host.h"
#include "test/integration/datapath-host.h"
#include "test/integration/settings.h"

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

  Stats GetStats() const;
  const Settings& settings() const { return settings_; }

  ControlpathHost* controlpath_sndr() const { return controlpath_sndr_.get(); }
  ControlpathHost* controlpath_rcvr() const { return controlpath_rcvr_.get(); }
  DatapathHost* datapath_sndr() const { return datapath_sndr_.get(); }
  DatapathHost* datapath_rcvr() const { return datapath_rcvr_.get(); }

 private:
  bool shouldContinue() const;
  void sendRequest();

 private:
  Settings settings_;
  std::atomic<bool> stop_{false};

  std::unique_ptr<ControlpathHost> controlpath_sndr_;
  std::unique_ptr<ControlpathHost> controlpath_rcvr_;
  std::unique_ptr<DatapathHost> datapath_sndr_;
  std::unique_ptr<DatapathHost> datapath_rcvr_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
