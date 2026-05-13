#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_

#include <cstdint>
#include <deque>
#include <thread>  // NOLINT
#include <type_traits>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/coding_style.h"

namespace peregrine {

// This class takes variable-sized transport requests, splits each of them into
// fixed-sized chunks, and schedules their execution on workers. Factors like
// CPU/memory/PCIs/NIC locality and network multipaths are key in this class
// to improve the performance of the transport operations.
// It is thread-safe.
class Engine {
  static_assert(assumptions::kTransportImplementationHasItsOwnThreads);
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  // Constructor.
  explicit Engine(int num_threads = 1);

  // Destructor.
  ~Engine();

  // Enqueues a valid transport request.
  absl::StatusOr<Handle> Enqueue(Endpoint peer, const Request& request);

  // Queries and updates the transport request identified by the `handle`.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

 private:
  // Generates a random handle.
  Handle genHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<Handle::ValueType, uint32_t>);
    return Handle(absl::Uniform<uint32_t>(bitgen_));
  }

  // Returns true iff there are pending requests or the destructor is called.
  bool hasWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Processes a single transport request.
  void processOne(Endpoint peer, const Request& request)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Runs in a worker thread to grab and process transport requests.
  void workerLoop(int i) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  struct Entry {
    Handle handle;
    EndpointStr peer;
    Request req;
  };

 private:
  absl::Mutex mu_;
  bool stopping_ ABSL_GUARDED_BY(mu_);
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> reqs_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<Handle, Status> sts_ ABSL_GUARDED_BY(mu_);

  std::vector<std::jthread> threads_;  // must be last to be destroyed first.
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
