#include "src/internal/rdma/rdma_device_manager.h"

#include <infiniband/verbs.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/internal/rdma/rdma_device_context.h"

namespace peregrine::internal {
namespace {

struct ibv_device** getDeviceList(int& num_devices) {
  num_devices = 0;
  struct ibv_device** device_list = ibv_get_device_list(&num_devices);
  if (device_list == nullptr || num_devices == 0) {
    if (device_list != nullptr) {
      ibv_free_device_list(device_list);
    }
    LOG(INFO) << "No RDMA devices found via ibv_get_device_list";
    return nullptr;
  }
  return device_list;
}

std::unique_ptr<RdmaDeviceContext> openDeviceContext(struct ibv_device* dev) {
  std::unique_ptr<RdmaDeviceContext> dev_ctx = RdmaDeviceContext::Create(dev);
  if (dev_ctx == nullptr) {
    const char* name = ibv_get_device_name(dev);
    LOG(WARNING) << "Failed to open and initialize RDMA adapter: "
                 << (name != nullptr ? name : "unknown");
  }
  return dev_ctx;
}

}  // namespace

RdmaDeviceManager::RdmaDeviceManager(
    std::vector<std::unique_ptr<RdmaDeviceContext>> devices,
    absl::flat_hash_map<std::string, RdmaDeviceContext*> device_map)
    : devices_(std::move(devices)), device_map_(std::move(device_map)) {
  LOG(INFO) << "RdmaDeviceManager initialized with " << devices_.size()
            << " device(s)";
}

RdmaDeviceManager::~RdmaDeviceManager() {
  LOG(INFO) << "RdmaDeviceManager destroyed";
}

RdmaDeviceContext* RdmaDeviceManager::GetDevice(std::string_view name) const {
  auto it = device_map_.find(name);
  if (it == device_map_.end()) {
    return nullptr;
  }
  return it->second;
}

absl::StatusOr<std::unique_ptr<RdmaDeviceManager>> RdmaDeviceManager::Create() {
  int num_devices = 0;
  struct ibv_device** device_list = getDeviceList(num_devices);
  if (device_list == nullptr) {
    return absl::NotFoundError(
        "no active RDMA Host Channel Adapters found on this host");
  }

  std::vector<std::unique_ptr<RdmaDeviceContext>> devices;
  absl::flat_hash_map<std::string, RdmaDeviceContext*> device_map;

  for (int i = 0; i < num_devices; ++i) {
    struct ibv_device* dev = device_list[i];
    if (dev == nullptr) {
      continue;
    }

    std::unique_ptr<RdmaDeviceContext> dev_ctx = openDeviceContext(dev);
    if (dev_ctx == nullptr) {
      continue;
    }

    RdmaDeviceContext* ptr = dev_ctx.get();
    device_map[std::string(ptr->Name())] = ptr;
    devices.push_back(std::move(dev_ctx));
  }

  ibv_free_device_list(device_list);

  if (devices.empty()) {
    return absl::NotFoundError(
        "failed to initialize or open any available RDMA adapters");
  }

  return absl::WrapUnique(
      new RdmaDeviceManager(std::move(devices), std::move(device_map)));
}

}  // namespace peregrine::internal
