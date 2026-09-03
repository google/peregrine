#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_ACCEPTOR_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_ACCEPTOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "third_party/gloop/util/random/shared_bit_gen.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel.h"
#include "src/internal/control/control.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/rdma/rdma_device_manager.h"
#include "src/internal/rdma/rdma_memory_manager.h"
#include "src/internal/rdma/rdma_queue_pair.h"

namespace peregrine::internal {

// RdmaAcceptor manages the RDMA data plane. It owns the device manager,
// memory manager, and inbound passive Queue Pairs. It registers and serves
// inbound RDMA connect requests via Control, handles outbound connection
// establishment, and provides thread-safe memory registration.
//
// This class is thread-safe.
class RdmaAcceptor final {
 public:
  // Factory method to create an RdmaAcceptor.
  static std::unique_ptr<RdmaAcceptor> Create(const Config& config,
                                              HostInfo& self, Control& control);

  // Destructor.
  ~RdmaAcceptor();

  // Connects to a peer via RDMA and returns established RDMA channels.
  std::vector<std::unique_ptr<Channel>> Connect(const Endpoint& peer,
                                                int num_conns);

  // Registers a contiguous memory buffer across all active RDMA hardware
  // adapters.
  absl::Status RegisterMemory(void* addr, size_t length)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Deregisters a previously registered memory buffer from active RDMA
  // hardware adapters.
  absl::Status DeregisterMemory(const void* addr) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Constructor.
  RdmaAcceptor(const Config& config, const HostInfo& self, Control& control,
               std::unique_ptr<RdmaDeviceManager> rdma_devmgr);

  // Handles an incoming RDMA connection request from a remote peer.
  absl::Status handleConnect(const proto::RdmaConnectRequest& req,
                             proto::RdmaConnectResponse* resp);

  // Generates a random packet sequence number.
  uint32_t genPsn();

 private:
  const Config& config_;
  const HostInfo& self_;
  Control& control_;

  // RDMA device manager discovering and managing host HCAs. Read-only after
  // construction, so concurrent access is thread-safe without mutex locking.
  std::unique_ptr<RdmaDeviceManager> rdma_devmgr_;
  util_random::SharedBitGen bitgen_;

  mutable absl::Mutex mu_;
  std::unique_ptr<RdmaMemoryManager> rdma_memmgr_ ABSL_GUARDED_BY(mu_);

  // Passive Queue Pairs created for incoming connections from remote peers.
  // Must be retained here to prevent RAII destruction (~RdmaQueuePair() calls
  // ibv_destroy_qp()), keeping the hardware QPs alive on the NIC so it can
  // accept incoming one-sided RDMA writes.
  // (Outbound active QPs are transferred to the Channels returned by
  // Connect()).
  std::vector<std::unique_ptr<RdmaQueuePair>> inbound_rdma_qps_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_ACCEPTOR_H_
