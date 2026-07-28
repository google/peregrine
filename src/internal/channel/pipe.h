#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_PIPE_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_PIPE_H_

#include <deque>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/util/test_iov.h"

namespace peregrine::internal::testing {

// A unidirectional pipe for in-process communication.
// It is thread-safe.
struct MemPipe final {
  mutable absl::Mutex mu;
  bool shutdown ABSL_GUARDED_BY(mu);
  std::deque<OwnedIoVec> queue ABSL_GUARDED_BY(mu);

  // Constructor.
  MemPipe();

  // Destructor.
  ~MemPipe();

  // Shuts down the pipe so no more send/recv calls will be taken.
  void Shutdown();
};

// A bidirectional pipe for in-process communication.
// It is thread-safe.
class BidiPipe final {
 public:
  // Creates a pair of crossed bidirectional pipes.
  static std::pair<BidiPipe, BidiPipe> Create();

  // Returns the input pipe.
  std::shared_ptr<MemPipe> InputPipe() const { return in_; }

  // Returns the output pipe.
  std::shared_ptr<MemPipe> OutputPipe() const { return out_; }

 private:
  // Constructor.
  BidiPipe(std::shared_ptr<MemPipe> in, std::shared_ptr<MemPipe> out)
      : in_(std::move(in)), out_(std::move(out)) {}

 private:
  std::shared_ptr<MemPipe> in_;
  std::shared_ptr<MemPipe> out_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_PIPE_H_
