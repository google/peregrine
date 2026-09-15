#ifndef PEREGRINE_TEST_INTEGRATION_RUNNER_H_
#define PEREGRINE_TEST_INTEGRATION_RUNNER_H_

#include <atomic>

#include "absl/time/clock.h"
#include "src/util/thread.h"

namespace peregrine::integration {

// This template class wraps a runnable object and runs it periodically.
template <typename Runnable>
class Runner final {
 public:
  explicit Runner(Runnable* r) : runnable_(r), stop_(false) {
    if (runnable_ != nullptr) {
      thread_ = util::Jthread([this]() {
        while (!stop_.load()) {
          runnable_->Run();
          absl::SleepFor(runnable_->Cycle());
        }
      });
    }
  }

  ~Runner() { stop_.store(true); }

 private:
  Runnable* runnable_;
  std::atomic<bool> stop_;
  util::Jthread thread_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_RUNNER_H_
