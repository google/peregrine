#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONTEXT_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONTEXT_H_

#include <infiniband/verbs.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class represents the hardware context for a single opened RDMA device.
// It owns the underlying ibv_context, Protection Domain (PD), and Completion
// Queue (CQ) for the physical adapter.
// It is thread-compatible but not thread-safe.
class RdmaContext final {
 public:
  // Default physical port number for single-port RDMA adapters.
  // TODO: Support multi-port HCAs if physical adapters expose multiple active
  // ports.
  static constexpr uint8_t kDefaultPort = 1;

  // Creates an RdmaContext by opening the given verbs device.
  // Returns nullptr on failure.
  static std::unique_ptr<RdmaContext> Create(struct ibv_device* device);

  // Disallows copy and move.
  DISALLOW_COPY(RdmaContext);
  DISALLOW_MOVE(RdmaContext);

  // Destructor.
  ~RdmaContext();

  // Returns the name of the physical RDMA device.
  std::string_view Name() const { return name_; }

  // Returns the open verbs context pointer.
  struct ibv_context* GetIbvContext() const { return ctx_; }

  // Returns the protection domain for this device.
  struct ibv_pd* GetPd() const { return pd_; }

  // Returns the completion queue for this device.
  struct ibv_cq* GetCq() const { return cq_; }

  // Returns the cached hardware device attributes.
  const struct ibv_device_attr& GetDeviceAttr() const { return attr_; }

  // Returns the routable GID index for this device (e.g. RoCEv2 IPv4 slot).
  int GidIndex() const { return gid_index_; }

  // Returns the probed local GID for this device.
  const union ibv_gid& LocalGid() const { return local_gid_; }

 private:
  // Constructor.
  RdmaContext(struct ibv_context* ctx, struct ibv_pd* pd, struct ibv_cq* cq,
              int gid_index, const union ibv_gid& local_gid,
              const struct ibv_device_attr& attr);

 private:
  // TODO: transition to multiple CQs per device (one per polling worker thread)
  // to enable zero-contention lock-free polling across multiple QPs.
  const std::string name_;
  struct ibv_context* ctx_;
  struct ibv_pd* pd_;
  struct ibv_cq* cq_;
  const int gid_index_;
  const union ibv_gid local_gid_;
  const struct ibv_device_attr attr_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONTEXT_H_
