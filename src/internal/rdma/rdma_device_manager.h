#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_MANAGER_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class manages the available RDMA devices on the host. It enumerates and
// opens all active RDMA Host Channel Adapters (HCAs).
//
// It is thread-compatible but not thread-safe.
class RdmaDeviceManager final {
  // TODO: Handle async events that indicate failures (QP timeouts, device
  // faults, etc.) here or in RdmaDeviceContext. Each `ibv_context` contains an
  // `async_fd` member that is used with epoll to pull these events from the
  // kernel.

 public:
  // Creates an RdmaDeviceManager by discovering and opening all system HCAs.
  static absl::StatusOr<std::unique_ptr<RdmaDeviceManager>> Create();

  // Disallows copy and move.
  DISALLOW_COPY(RdmaDeviceManager);
  DISALLOW_MOVE(RdmaDeviceManager);

  // Destructor.
  ~RdmaDeviceManager();

  // Returns all open hardware RDMA device contexts.
  absl::Span<const std::unique_ptr<RdmaDeviceContext>> Devices() const {
    return devices_;
  }

  // Returns the device context for the specified adapter name, or nullptr if
  // not found or unopened.
  RdmaDeviceContext* GetDevice(std::string_view name) const;

 private:
  // Constructor.
  RdmaDeviceManager(
      std::vector<std::unique_ptr<RdmaDeviceContext>> devices,
      absl::flat_hash_map<std::string, RdmaDeviceContext*> device_map);

 private:
  std::vector<std::unique_ptr<RdmaDeviceContext>> devices_;
  absl::flat_hash_map<std::string, RdmaDeviceContext*> device_map_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_MANAGER_H_
