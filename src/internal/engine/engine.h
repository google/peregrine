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
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/coding_style.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/worker.h"
#include "src/internal/rdma/rdma_device_manager.h"
#include "src/internal/rdma/rdma_memory_manager.h"
#include "src/internal/rdma/rdma_queue_pair.h"
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
class Engine final {
  static_assert(assumptions::kTransportImplementationHasItsOwnThreads);
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  // Creates an engine.
  static std::unique_ptr<Engine> Create(const Config& config, HostInfo& self,
                                        Control& control);

  // Destructor.
  ~Engine();

  // Enqueues a number of valid transport request.
  absl::StatusOr<Handle> Enqueue(const Endpoint& peer,
                                 absl::Span<const Request> requests);

  // Queries and updates the transport request identified by the `handle`.
  absl::StatusOr<Status> QueryUpdate(Handle handle);

  // Registers a contiguous memory buffer across active RDMA hardware adapters.
  absl::Status RegisterMemory(void* addr, size_t length)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Deregisters a previously registered memory buffer from active RDMA hardware
  // adapters.
  absl::Status DeregisterMemory(const void* addr) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns the RDMA memory manager if initialized.
  const RdmaMemoryManager* GetMemoryManager() const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock _(mu_);
    return rdma_memory_manager_.get();
  }

 private:
  struct Entry {
    Endpoint peer;
    Handle handle;
    ReqId reqid;
    Request request;
  };

 private:
  // Constructor.
  Engine(const Config& config, HostInfo& self,
         std::unique_ptr<TcpAcceptor> acceptor,
         std::unique_ptr<RdmaDeviceManager> rdma_device_manager,
         Control& control);

 private:
  using Workers = std::vector<std::unique_ptr<Worker>>;

  // Accepts the incoming `socket`.
  void accept(std::unique_ptr<TcpSocket> socket);

  // Connects to the `peer` to create a number of workers.
  bool connect(Workers& workers, const Endpoint& peer);

  // Connects to a TCP `peer`.
  bool connectTcp(Workers& workers, const Endpoint& peer);

  // Connects to an RDMA `peer`.
  bool connectRdma(Workers& workers, const Endpoint& peer);

  // Handles an incoming RDMA connection request from a remote peer.
  absl::Status handleRdmaConnect(const proto::RdmaConnectRequest& req,
                                 proto::RdmaConnectResponse* resp);

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
  const Config& config_;
  HostInfo& self_;
  Control& control_;

  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> reqs_ ABSL_GUARDED_BY(mu_);

  RequestTracker outgoing_;
  RequestTracker incoming_;

  // TCP data plane.
  std::unique_ptr<TcpAcceptor> acceptor_;

  // RDMA data plane.
  std::unique_ptr<RdmaDeviceManager> rdma_device_manager_;
  std::unique_ptr<RdmaMemoryManager> rdma_memory_manager_ ABSL_GUARDED_BY(mu_);
  std::vector<std::unique_ptr<RdmaQueuePair>> qps_ ABSL_GUARDED_BY(mu_);

  absl::flat_hash_map<Endpoint, Workers> send_workers_;
  Workers recv_workers_;
  std::jthread acceptor_thread_;
  std::jthread main_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_H_
