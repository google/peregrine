#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/coding_style.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/control/message.pb.h"
#include "peregrine/src/internal/control/message_internal.pb.h"
#include "peregrine/src/internal/engine/engine_helper.h"
#include "peregrine/src/internal/engine/worker.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {

// This class takes variable-sized transport requests, splits each of them into
// fixed-sized chunks, and schedules their execution on workers. Factors like
// CPU/memory/PCIe/NIC locality and network multipaths are key in this class
// to improve the transport performance.
// It is thread-safe.
class Engine final {
  static_assert(assumptions::kTransportImplementationHasItsOwnThreads);
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  // A transport request item.
  struct Item {
    absl::Span<const Request> requests;
    absl::Time start_time;
    absl::AnyInvocable<void(Status)> on_complete;
    // Returns true iff the item has at least one request and all are valid.
    bool IsValid() const;
  };

  // Creates an engine.
  static std::unique_ptr<Engine> Create(const Config& config, HostInfo& self,
                                        Control& control);

  // Disallows copy and move.
  DISALLOW_COPY(Engine);
  DISALLOW_MOVE(Engine);

  // Destructor.
  ~Engine();

  // Returns the engine helper.
  EngineHelper* absl_nonnull Helper() const { return helper_.get(); }

  // Enqueues a transport request item.
  absl::StatusOr<Handle> Enqueue(const Endpoint& peer, Item item);

  // Queries and updates the transport request identified by the `handle`.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

  // Takes a snapshot of engine metrics.
  void GetMetrics(TransportMetrics& m) const { metrics_.Snapshot(m); }

 private:
  struct Entry {
    Endpoint peer;
    Handle handle;
    ReqId reqid;
    Request request;
  };

 private:
  // Constructor.
  Engine(const Config& config, const HostInfo& self,
         std::unique_ptr<EngineHelper> helper);

  // Generates a unique handle.
  Handle genHandle() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<Handle::ValueType, uint32_t>);
    return Handle(++next_handle_);
  }

  // Generates a unique request id.
  ReqId genReqId() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    static_assert(std::is_same_v<ReqId::ValueType, uint32_t>);
    return ReqId(++next_reqid_);
  }

  // Returns the tracker for the request.
  RequestTracker& getRequestTracker(const Request& request) {
    return request.op == Op::kWrite ? outgoing_ : incoming_;
  }

  // Returns the per-op metrics for the request.
  OpMetrics& getOpMetrics(const Request& request) {
    return request.op == Op::kWrite ? metrics_.write : metrics_.read;
  }

  // Runs in a tcp manager thread to create tcp channels.
  void tcpmgrLoop();

  // Returns true iff there are pending requests or the destructor is called.
  bool hasWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Runs in a main thread to grab and process transport requests.
  void mainLoop() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  using Workers = std::vector<std::unique_ptr<Worker>>;

  // Creates a worker, using positive/negative id for send/recv respectively.
  std::unique_ptr<Worker> createWorker(int id, std::unique_ptr<Channel> ch) {
    return std::make_unique<Worker>(id, self_, outgoing_, incoming_, metrics_,
                                    std::move(ch));
  }

  // Creates send workers for a `peer`.
  void createSendWorkers(const Endpoint& peer);

  // Creates recv workers for accepted channels.
  bool createRecvWorkers();

  // Processes a single entry.
  void process(const Entry& entry) ABSL_LOCKS_EXCLUDED(mu_);

  // Processes a write request.
  void processWrite(Workers& workers, Handle handle, ReqId reqid,
                    const Request& request) ABSL_LOCKS_EXCLUDED(mu_);

  // Processes a read request.
  void processRead(Handle handle, ReqId reqid, const Request& request)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  const Config& config_;
  const HostInfo& self_;

  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  Handle::ValueType next_handle_ ABSL_GUARDED_BY(mu_);
  ReqId::ValueType next_reqid_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> reqs_ ABSL_GUARDED_BY(mu_);

  RequestTracker outgoing_;
  RequestTracker incoming_;

  EngineMetrics& metrics_;

  absl_nonnull std::unique_ptr<EngineHelper> helper_;
  absl::flat_hash_map<Endpoint, Workers> send_workers_;
  absl::flat_hash_map<Endpoint, Workers> recv_workers_;

  util::Jthread tcpmgr_thread_;
  util::Jthread main_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
