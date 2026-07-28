#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_PIPE_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_PIPE_H_

#include <deque>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/util/test_iov.h"

namespace peregrine::internal::testing {

// A unidirectional pipe for in-process communication.
struct MemPipe final {
  mutable absl::Mutex mu;
  bool is_shutdown ABSL_GUARDED_BY(mu) = false;
  std::deque<OwnedIoVec> queue ABSL_GUARDED_BY(mu);

  // Shuts down the pipe so no more send/recv calls will be taken.
  void Shutdown() {
    absl::MutexLock lock(mu);
    is_shutdown = true;
  }
};

// A bidirectional pipe for in-process communication.
struct BidiPipe final {
  const std::shared_ptr<MemPipe> out_pipe;
  const std::shared_ptr<MemPipe> in_pipe;

  // Creates a pair of crossed bidirectional pipes.
  static std::pair<BidiPipe, BidiPipe> Create() {
    auto p1 = std::make_shared<MemPipe>();
    auto p2 = std::make_shared<MemPipe>();
    return {BidiPipe{/*out_pipe=*/p1, /*in_pipe=*/p2},
            BidiPipe{/*out_pipe=*/p2, /*in_pipe=*/p1}};
  }
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_PIPE_H_
