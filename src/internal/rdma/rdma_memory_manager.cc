#include "src/internal/rdma/rdma_memory_manager.h"

#include <infiniband/verbs.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/internal/rdma/rdma_device_manager.h"

namespace peregrine::internal {
namespace {

std::string ErrorMsg(std::string_view prefix, int last_errno) {
  return absl::StrFormat("%s: errno=%d (%s)", prefix, last_errno,
                         std::strerror(last_errno));
}

void deregisterMrs(
    const absl::flat_hash_map<std::string, struct ibv_mr*>& mrs) {
  for (const auto& [device_name, mr] : mrs) {
    if (mr != nullptr) {
      if (ibv_dereg_mr(mr) != 0) {
        LOG(WARNING) << ErrorMsg(
            absl::StrFormat("failed to deregister MR on device %s",
                            device_name),
            errno);
      }
    }
  }
}

}  // namespace

RdmaMemoryManager::RdmaMemoryManager(const RdmaDeviceManager* device_manager)
    : device_manager_(device_manager) {
  DCHECK_NE(device_manager_, nullptr);
  LOG(INFO) << "RdmaMemoryManager initialized";
}

RdmaMemoryManager::~RdmaMemoryManager() {
  for (const auto& [addr, region] : registered_regions_) {
    deregisterMrs(region.device_mrs);
  }
  LOG(INFO) << "RdmaMemoryManager destroyed";
}

absl::Status RdmaMemoryManager::RegisterMemory(void* addr, size_t length,
                                               int access_flags) {
  if (addr == nullptr) {
    return absl::InvalidArgumentError("addr cannot be nullptr");
  }
  if (length == 0) {
    return absl::InvalidArgumentError("length must be greater than 0");
  }
  if (device_manager_ == nullptr) {
    return absl::FailedPreconditionError("device_manager cannot be nullptr");
  }
  if (device_manager_->Devices().empty()) {
    return absl::FailedPreconditionError(
        "no active RDMA devices available to register memory");
  }

  // TODO: We currently assume memory buffers are registered as cohesive,
  // single slabs. We do not support piecewise or chunked partial registration
  // of sub-regions within a larger unpinned buffer.
  if (findRegion(addr, length) != nullptr) {
    return absl::AlreadyExistsError(absl::StrFormat(
        "buffer range [%p, %p) is already registered", addr,
        reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(addr) +
                                      length)));
  }

  absl::flat_hash_map<std::string, struct ibv_mr*> memory_regions;

  for (const auto& dev_ctx : device_manager_->Devices()) {
    struct ibv_pd* pd = dev_ctx->GetPd();
    if (pd == nullptr) {
      deregisterMrs(memory_regions);
      return absl::InternalError(absl::StrFormat(
          "device %s has null protection domain (PD)", dev_ctx->Name()));
    }

    struct ibv_mr* mr = ibv_reg_mr(pd, addr, length, access_flags);
    if (mr == nullptr) {
      const int err = errno;
      LOG(WARNING) << ErrorMsg(
          absl::StrFormat("ibv_reg_mr failed for device %s (addr=%p, len=%zu)",
                          dev_ctx->Name(), addr, length),
          err);
      deregisterMrs(memory_regions);
      return absl::InternalError(
          absl::StrFormat("ibv_reg_mr failed on device %s: errno=%d (%s)",
                          dev_ctx->Name(), err, std::strerror(err)));
    }

    memory_regions[std::string(dev_ctx->Name())] = mr;
  }

  LOG(INFO) << "Registered memory buffer at " << addr << " (length " << length
            << " bytes) across " << memory_regions.size() << " device(s)";
  registered_regions_[reinterpret_cast<uintptr_t>(addr)] =
      RegisteredRegion{addr, length, std::move(memory_regions)};
  return absl::OkStatus();
}

absl::Status RdmaMemoryManager::DeregisterMemory(const void* addr) {
  const uintptr_t target = reinterpret_cast<uintptr_t>(addr);
  auto it = registered_regions_.find(target);
  if (it == registered_regions_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("buffer at %p is not registered", addr));
  }

  deregisterMrs(it->second.device_mrs);
  registered_regions_.erase(it);
  LOG(INFO) << "Deregistered memory buffer at " << addr;
  return absl::OkStatus();
}

// Finds the registered region that wholly contains [addr, addr + length).
// Only single-slab containment is supported; multi-slab spanning is not
// supported as verbs SGE operations require a single LKey per contiguous
// transfer.
const RdmaMemoryManager::RegisteredRegion* RdmaMemoryManager::findRegion(
    const void* addr, size_t length) const {
  if (addr == nullptr || length == 0 || registered_regions_.empty()) {
    return nullptr;
  }

  const uintptr_t req_start = reinterpret_cast<uintptr_t>(addr);
  const uintptr_t req_end = req_start + length;

  // Find the first region that has higher base address than the requested
  // buffer.
  auto it = registered_regions_.upper_bound(req_start);
  if (it == registered_regions_.begin()) {
    return nullptr;
  }

  // Check the previous region to see if it contains the requested buffer.
  --it;
  const uintptr_t region_start = it->first;
  const uintptr_t region_end = region_start + it->second.length;

  if (req_start >= region_start && req_end <= region_end) {
    return &it->second;
  }
  return nullptr;
}

struct ibv_mr* RdmaMemoryManager::GetMemoryRegion(
    const void* addr, size_t length, std::string_view device_name) const {
  const auto* region = findRegion(addr, length);
  if (region == nullptr) {
    return nullptr;
  }
  auto mr_it = region->device_mrs.find(device_name);
  if (mr_it == region->device_mrs.end()) {
    return nullptr;
  }
  return mr_it->second;
}

absl::StatusOr<uint32_t> RdmaMemoryManager::GetLKey(
    const void* addr, size_t length, std::string_view device_name) const {
  struct ibv_mr* mr = GetMemoryRegion(addr, length, device_name);
  if (mr == nullptr) {
    return absl::NotFoundError(absl::StrFormat(
        "no MR found covering range [%p, %p) on device %s", addr,
        reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(addr) +
                                      length),
        device_name));
  }
  return mr->lkey;
}

absl::StatusOr<uint32_t> RdmaMemoryManager::GetRKey(
    const void* addr, size_t length, std::string_view device_name) const {
  struct ibv_mr* mr = GetMemoryRegion(addr, length, device_name);
  if (mr == nullptr) {
    return absl::NotFoundError(absl::StrFormat(
        "no MR found covering range [%p, %p) on device %s", addr,
        reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(addr) +
                                      length),
        device_name));
  }
  return mr->rkey;
}

}  // namespace peregrine::internal
