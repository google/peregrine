#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_

#include <cstdint>
#include <deque>
#include <memory>
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
#include "src/internal/base/endpoint.h"
#include "src/internal/coding_style.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This class takes variable-sized transport requests, splits each of them into
// fixed-sized chunks, and schedules their execution on workers. Factors like
// CPU/memory/PCIe/NIC locality and network multipaths are key in this class
// to improve the transport performance.
// It is thread-safe.
class Engine {
  static_assert(assumptions::kTransportImplementationHasItsOwnThreads);
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  // Constructor.
  explicit Engine(std::unique_ptr<TcpAcceptor> acceptor);

  // Destructor.
  ~Engine();

  // Enqueues a valid transport request.
  absl::StatusOr<Handle> Enqueue(const Endpoint& peer, const Request& request);

  // Queries and updates the transport request identified by the `handle`.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

 private:
  // Acceptor callback with the given `socket`.
  void accept(std::unique_ptr<TcpSocket> socket);

  // Generates a random handle.
  Handle genHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<Handle::ValueType, uint32_t>);
    return Handle(absl::Uniform<uint32_t>(bitgen_));
  }

  // Returns true iff there are pending requests or the destructor is called.
  bool hasWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Processes a single transport request.
  void processOne(const Endpoint& peer, const Request& request)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Runs in a worker thread to grab and process transport requests.
  void workerLoop(int i) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  struct Entry {
    Handle handle;
    Endpoint peer;
    Request req;
  };

 private:
  absl::Mutex mu_;
  bool stopping_ ABSL_GUARDED_BY(mu_);
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> reqs_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<Handle, Status> sts_ ABSL_GUARDED_BY(mu_);

  std::unique_ptr<TcpAcceptor> acceptor_;
  std::jthread acceptor_thread_;

  std::vector<std::jthread> threads_;  // must be last to be destroyed first.
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
