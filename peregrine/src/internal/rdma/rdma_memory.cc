#include "peregrine/src/internal/rdma/rdma_memory.h"

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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/internal/rdma/rdma_context.h"
#include "peregrine/src/internal/rdma/rdma_device.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {
namespace {

using util::Errno;

std::string ErrMsg(const std::string_view prefix, const Errno err) {
  return absl::StrFormat("%s: errno=%d (%s)", prefix, err.value(),
                         std::strerror(err.value()));
}

void UnregisterMrs(
    const absl::flat_hash_map<std::string, struct ibv_mr*>& mrs) {
  for (const auto& [device_name, mr] : mrs) {
    if (mr != nullptr && ibv_dereg_mr(mr) != 0) {
      const Errno err(errno);
      LOG(WARNING) << ErrMsg(
          absl::StrCat("failed to unregister MR on device ", device_name), err);
    }
  }
}
}  // namespace

RdmaMemory::RdmaMemory(const RdmaDevice* dev) : dev_(dev) {
  DCHECK_NE(dev_, nullptr);
  LOG(INFO) << "RdmaMemory initialized";
}

RdmaMemory::~RdmaMemory() {
  for (const auto& [addr, region] : registered_regions_) {
    UnregisterMrs(region.device_mrs);
  }
  LOG(INFO) << "RdmaMemory destroyed";
}

absl::Status RdmaMemory::RegisterMemory(void* addr, size_t length,
                                        int access_flags) {
  if (addr == nullptr) {
    return absl::InvalidArgumentError("addr cannot be nullptr");
  }
  if (length == 0) {
    return absl::InvalidArgumentError("length must be greater than 0");
  }
  if (dev_ == nullptr) {
    return absl::FailedPreconditionError("device_manager cannot be nullptr");
  }
  if (dev_->Contexts().empty()) {
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

  for (const auto& crx : dev_->Contexts()) {
    struct ibv_pd* pd = crx->GetPd();
    if (pd == nullptr) {
      UnregisterMrs(memory_regions);
      return absl::InternalError(absl::StrFormat(
          "device %s has null protection domain (PD)", crx->Name()));
    }

    struct ibv_mr* mr = ibv_reg_mr(pd, addr, length, access_flags);
    if (mr == nullptr) {
      const Errno err(errno);
      LOG(WARNING) << ErrMsg(
          absl::StrFormat("ibv_reg_mr failed for device %s (addr=%p, len=%zu)",
                          crx->Name(), addr, length),
          err);
      UnregisterMrs(memory_regions);
      return absl::InternalError(absl::StrFormat(
          "ibv_reg_mr failed on device %s: errno=%d (%s)", crx->Name(),
          err.value(), std::strerror(err.value())));
    }

    memory_regions[std::string(crx->Name())] = mr;
  }

  LOG(INFO) << "Registered memory buffer at " << addr << " (length " << length
            << " bytes) across " << memory_regions.size() << " device(s)";
  registered_regions_[reinterpret_cast<uintptr_t>(addr)] =
      RegisteredRegion{addr, length, std::move(memory_regions)};
  return absl::OkStatus();
}

absl::Status RdmaMemory::UnregisterMemory(const void* addr) {
  const uintptr_t target = reinterpret_cast<uintptr_t>(addr);
  auto it = registered_regions_.find(target);
  if (it == registered_regions_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("buffer at %p is not registered", addr));
  }

  UnregisterMrs(it->second.device_mrs);
  registered_regions_.erase(it);
  LOG(INFO) << "Unregistered memory buffer at " << addr;
  return absl::OkStatus();
}

// Finds the registered region that wholly contains [addr, addr + length).
// Only single-slab containment is supported; multi-slab spanning is not
// supported as verbs SGE operations require a single LKey per contiguous
// transfer.
const RdmaMemory::RegisteredRegion* RdmaMemory::findRegion(
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

struct ibv_mr* RdmaMemory::GetMemoryRegion(const void* addr, size_t length,
                                           std::string_view device_name) const {
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

absl::StatusOr<uint32_t> RdmaMemory::GetLKey(
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

absl::StatusOr<uint32_t> RdmaMemory::GetRKey(
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

uint32_t RdmaMemory::GetDefaultLKey(std::string_view device_name) const {
  if (registered_regions_.empty()) {
    return 0;
  }
  const auto& region = registered_regions_.begin()->second;
  auto mr_it = region.device_mrs.find(std::string(device_name));
  if (mr_it == region.device_mrs.end() || mr_it->second == nullptr) {
    return 0;
  }
  return mr_it->second->lkey;
}

uint32_t RdmaMemory::GetDefaultRKey(std::string_view device_name) const {
  if (registered_regions_.empty()) {
    return 0;
  }
  const auto& region = registered_regions_.begin()->second;
  auto mr_it = region.device_mrs.find(std::string(device_name));
  if (mr_it == region.device_mrs.end() || mr_it->second == nullptr) {
    return 0;
  }
  return mr_it->second->rkey;
}

}  // namespace peregrine::internal
