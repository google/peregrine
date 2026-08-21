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
#include "absl/random/random.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/coding_style.h"
#include "src/internal/engine/worker.h"
#include "src/internal/request/request_tracker.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/util/util.h"

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
  // Creates an engine.
  static std::unique_ptr<Engine> Create(int num_conns_per_peer, HostInfo& self);

  // Destructor.
  ~Engine();

  // Enqueues a number of valid transport request.
  absl::StatusOr<Handle> Enqueue(const Endpoint& peer,
                                 absl::Span<const Request> requests);

  // Queries and updates the transport request identified by the `handle`.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

 private:
  struct Entry {
    Endpoint peer;
    Handle handle;
    ReqId reqid;
    Request request;
  };

 private:
  // Constructor.
  Engine(std::unique_ptr<TcpAcceptor> acceptor, int num_conns_per_peer,
         HostInfo& self);

 private:
  using Workers = std::vector<std::unique_ptr<Worker>>;

  // Accepts the incoming `socket`.
  void accept(std::unique_ptr<TcpSocket> socket);

  // Connects to the `peer` to create a number of workers.
  bool connect(Workers& workers, const Endpoint& peer);

 private:
  // Generates a random handle.
  Handle genHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<Handle::ValueType, uint32_t>);
    return Handle(util::Random<Handle::ValueType>(bitgen_));
  }

  // Generates a random request id.
  ReqId genReqId() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<ReqId::ValueType, uint32_t>);
    return ReqId(util::Random<ReqId::ValueType>(bitgen_));
  }

  // Returns the tracker for the request.
  RequestTracker& getRequestTracker(const Request& request) {
    return request.op == Op::kWrite ? outgoing_ : incoming_;
  }

  // Returns true iff there are pending requests or the destructor is called.
  bool hasWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Runs in a main thread to grab and process transport requests.
  void mainLoop() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Processes a single entry.
  void process(const Entry& entry) ABSL_LOCKS_EXCLUDED(mu_);

  // Processes a write request.
  void processWrite(Workers& workers, Handle handle, ReqId reqid,
                    const Request& request) ABSL_LOCKS_EXCLUDED(mu_);

  // Processes a read request.
  void processRead(Handle handle, ReqId reqid, const Request& request)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  HostInfo& self_;
  const int num_conns_per_peer_;

  absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> reqs_ ABSL_GUARDED_BY(mu_);

  RequestTracker outgoing_;
  RequestTracker incoming_;

  std::unique_ptr<TcpAcceptor> acceptor_;
  absl::flat_hash_map<Endpoint, Workers> send_workers_;
  Workers recv_workers_;
  std::jthread acceptor_thread_;
  std::jthread main_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
