#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/rdma/rdma_context.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class manages the available RDMA devices on the host. It enumerates and
// opens all active RDMA Host Channel Adapters (HCAs).
//
// TODO: Handle async events that indicate failures (QP timeouts, device
// faults, etc.) here or in RdmaContext. Each `ibv_context` contains an
// `async_fd` member that is used with epoll to pull these events from the
// kernel.
//
// It is thread-compatible but not thread-safe.
class RdmaDevice final {
 public:
  // Creates an RdmaDevice by discovering and opening all system HCAs.
  static absl::StatusOr<std::unique_ptr<RdmaDevice>> Create();

  // Disallows copy and move.
  DISALLOW_COPY(RdmaDevice);
  DISALLOW_MOVE(RdmaDevice);

  // Destructor.
  ~RdmaDevice();

  // Returns all open hardware RDMA device contexts.
  absl::Span<const std::unique_ptr<RdmaContext>> Contexts() const {
    return ctxs_;
  }

  // Returns the device context for the specified adapter name,
  // or nullptr if not found or unopened.
  RdmaContext* GetContext(std::string_view name) const;

 private:
  // Constructor.
  RdmaDevice(std::vector<std::unique_ptr<RdmaContext>> ctxs,
             absl::flat_hash_map<std::string, RdmaContext*> ctx_map);

 private:
  std::vector<std::unique_ptr<RdmaContext>> ctxs_;
  absl::flat_hash_map<std::string, RdmaContext*> ctx_map_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_DEVICE_H_
