#ifndef PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
#define PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "src/util/app.h"
#include "test/integration/settings.h"

namespace peregrine::integration {

class PeregrineIntegration {
 public:
  PeregrineIntegration();
  ~PeregrineIntegration();

  void Run();
  void Stop();

  int64_t n_transfers() const { return n_transfers_.load(); }
  int64_t n_bytes() const { return n_bytes_.load(); }

  const Settings& settings() const { return settings_; }

 private:
  void Wait();
  void SendRequest();
  bool Continue();


  std::unique_ptr<util::App> sender_;
  std::unique_ptr<util::App> receiver_;

  std::atomic<int64_t> n_transfers_{0};
  std::atomic<int64_t> n_bytes_{0};
  std::atomic<bool> stop_{false};

  Settings settings_;
};


}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_INTEGRATION_H_
