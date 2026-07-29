#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_CONTEXT_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_CONTEXT_H_

#include <infiniband/verbs.h>

#include <memory>
#include <string>
#include <string_view>

#include "src/util/macro.h"

namespace peregrine::internal {

// This class represents the hardware context for a single opened RDMA device.
// It owns the underlying ibv_context, Protection Domain (PD), and Completion
// Queue (CQ) for the physical adapter.
// It is thread-compatible but not thread-safe.
class RdmaDeviceContext final {
 public:
  // Creates an RdmaDeviceContext by opening the given verbs device.
  // Returns nullptr on failure.
  static std::unique_ptr<RdmaDeviceContext> Create(struct ibv_device* device);

  DISALLOW_COPY(RdmaDeviceContext);
  DISALLOW_MOVE(RdmaDeviceContext);

  // Destructor.
  ~RdmaDeviceContext();

  // Returns the name of the physical RDMA device.
  std::string_view Name() const { return name_; }

  // Returns the open verbs context pointer.
  struct ibv_context* GetDeviceContext() const { return context_; }

  // Returns the protection domain for this device.
  struct ibv_pd* GetPd() const { return pd_; }

  // Returns the completion queue for this device.
  struct ibv_cq* GetCq() const { return cq_; }

  // Returns the cached hardware device attributes.
  const struct ibv_device_attr& GetDeviceAttr() const { return device_attr_; }

 private:
  // Constructor.
  RdmaDeviceContext(struct ibv_context* context, struct ibv_pd* pd,
                    struct ibv_cq* cq,
                    const struct ibv_device_attr& device_attr);

 private:
  const std::string name_;
  struct ibv_context* context_;
  struct ibv_pd* pd_;
  // TODO: transition to multiple CQs per device (one per polling worker thread)
  // to enable zero-contention lock-free polling across multiple QPs.
  struct ibv_cq* cq_;
  const struct ibv_device_attr device_attr_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_CONTEXT_H_
