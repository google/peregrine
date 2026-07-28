#include "src/internal/channel/pipe.h"

#include <memory>
#include <utility>

#include "absl/synchronization/mutex.h"

namespace peregrine::internal::testing {

MemPipe::MemPipe() : mu(), shutdown(false), queue() {}

MemPipe::~MemPipe() {
  absl::MutexLock lock(mu);
  shutdown = true;
  queue.clear();
}

void MemPipe::Shutdown() {
  absl::MutexLock lock(mu);
  shutdown = true;
}

std::pair<BidiPipe, BidiPipe> BidiPipe::Create() {
  auto p1 = std::make_shared<MemPipe>();
  auto p2 = std::make_shared<MemPipe>();
  return {BidiPipe{/*in_pipe=*/p1, /*out_pipe=*/p2},
          BidiPipe{/*in_pipe=*/p2, /*out_pipe=*/p1}};
}

}  // namespace peregrine::internal::testing
