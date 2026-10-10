#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_MEMORY_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_MEMORY_H_

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/rdma/rdma_device.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class manages memory registration across all RDMA devices present in
// the RdmaDevice. It maintains an internal registry of registered memory
// regions and supports range-containment lookups for sub-buffers.
//
// It is thread-compatible but not thread-safe.
class RdmaMemory final {
  static_assert(assumptions::kPeregrineRegistersApplicationAllocatedMemory);

 public:
  // Default access flags for memory registration (local write, remote write,
  // remote read).
  static constexpr int kDefaultAccessFlags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

  // Constructor. `dev` must not be nullptr and must outlive this object.
  explicit RdmaMemory(const RdmaDevice* dev);

  // Disallows copy and move.
  DISALLOW_COPY(RdmaMemory);
  DISALLOW_MOVE(RdmaMemory);

  // Destructor. Automatically unregisters all memory regions across all
  // devices.
  ~RdmaMemory();

  // Pins and registers the memory buffer at `addr` of size `length` across all
  // active RDMA devices.
  // If registration fails on any device, all partially-registered handles in
  // this call are rolled back before returning an error status.
  absl::Status RegisterMemory(void* addr, size_t length,
                              int access_flags = kDefaultAccessFlags);

  // Unregisters the memory buffer registered at base `addr` across all devices.
  absl::Status UnregisterMemory(const void* addr);

  // Returns the ibv_mr handle for the range [addr, addr + length) on
  // `device_name`, or nullptr if the range is not registered or out of bounds.
  struct ibv_mr* GetMemoryRegion(const void* addr, size_t length,
                                 std::string_view device_name) const;

  // Convenience accessor for LKey of the range [addr, addr + length).
  absl::StatusOr<uint32_t> GetLKey(const void* addr, size_t length,
                                   std::string_view device_name) const;

  // Convenience accessor for RKey of the range [addr, addr + length).
  absl::StatusOr<uint32_t> GetRKey(const void* addr, size_t length,
                                   std::string_view device_name) const;

  // Returns the LKey of the first pre-registered memory region on
  // `device_name`, or 0 if no memory regions are registered.
  //
  // NOTE: This function is used during RDMA channel instantiation to resolve
  // the local LKey when no specific buffer address is known yet. It will be
  // deprecated once dynamic per-transfer key resolution is introduced.
  uint32_t GetDefaultLKey(std::string_view device_name) const;

  // Returns the RKey of the first pre-registered memory region on
  // `device_name`, or 0 if no memory regions are registered.
  //
  // NOTE: This function is used during the initial Rdma Connect handshake to
  // advertise the responder's pre-registered memory key when no specific buffer
  // address is known yet. It will be deprecated once dynamic per-transfer key
  // resolution (e.g. PeerRequests/PeerResponse) is introduced.
  uint32_t GetDefaultRKey(std::string_view device_name) const;

 private:
  // Internal record holding per-device ibv_mr handles for a registered buffer.
  struct RegisteredRegion {
    void* addr;
    size_t length;
    absl::flat_hash_map<std::string, struct ibv_mr*> device_mrs;
  };

  // Finds the registered region that wholly contains [addr, addr + length).
  // Multi-slab spanning (where a requested range crosses multiple distinct
  // registered slabs) is not supported because RDMA work requests require a
  // single LKey per contiguous SGE transfer.
  const RegisteredRegion* findRegion(const void* addr, size_t length) const;

 private:
  const RdmaDevice* const dev_;
  absl::btree_map<uintptr_t, RegisteredRegion> registered_regions_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_MEMORY_H_
