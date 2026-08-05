#ifndef PEREGRINE_TEST_INTEGRATION_RUNNER_H_
#define PEREGRINE_TEST_INTEGRATION_RUNNER_H_

#include <stop_token> // NOLINT
#include <thread>     // NOLINT

#include "absl/time/clock.h"

namespace peregrine::integration {

// This template class wraps a runnable object and runs it periodically.
template <typename Runnable>
class Runner final {
 public:
  explicit Runner(Runnable* r) : runnable_(r) {
    if (runnable_ != nullptr) {
      thread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
          runnable_->Run();
          // Note: `absl::SleepFor` does not monitor the `std::stop_token`.
          // `~Runner()` could block for up to `runnable_->Cycle()` on join.
          absl::SleepFor(runnable_->Cycle());
        }
      });
    }
  }

 private:
  Runnable* runnable_;
  std::jthread thread_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_RUNNER_H_
