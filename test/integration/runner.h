#ifndef PEREGRINE_TEST_INTEGRATION_RUNNER_H_
#define PEREGRINE_TEST_INTEGRATION_RUNNER_H_

#include <atomic>

#include "absl/base/nullability.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "src/util/thread.h"

namespace peregrine::integration {

// This template class wraps a runnable object and runs it periodically.
template <typename Runnable>
class Runner final {
 public:
  explicit Runner(Runnable* absl_nonnull r)
      : runnable_(r), stop_(false), thread_([this]() {
          while (!stop_.load()) {
            runnable_->Run();
            absl::SleepFor(runnable_->Cycle());
          }
        }) {
    DCHECK_NE(runnable_, nullptr);
  }

  void Stop() { stop_.store(true); }

  ~Runner() {
    Stop();
    thread_.join();
  }

 private:
  Runnable* runnable_;
  std::atomic<bool> stop_;
  util::Thread thread_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_RUNNER_H_
