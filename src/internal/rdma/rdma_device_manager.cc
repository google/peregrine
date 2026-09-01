#include "src/internal/rdma/rdma_device_manager.h"

#include <dirent.h>
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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "src/internal/rdma/rdma_device_context.h"

namespace peregrine::internal {
namespace {

bool ShouldIgnoreDevice(absl::string_view dev_name) {
  if (dev_name.empty()) return false;

  const std::string net_dir =
      absl::StrCat("/sys/class/infiniband/", dev_name, "/device/net");
  DIR* dir = opendir(net_dir.c_str());
  if (dir == nullptr) {
    return false;
  }

  bool all_dontuse = true;
  bool has_entries = false;
  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.') continue;
    has_entries = true;
    if (!absl::StrContainsIgnoreCase(entry->d_name, "dontuse")) {
      all_dontuse = false;
      break;
    }
  }
  closedir(dir);

  return has_entries && all_dontuse;
}

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

    const char* dev_name = ibv_get_device_name(dev);
    if (dev_name == nullptr) {
      continue;
    }

    // TODO: This is a temporary workaround that specifically ignores interfaces
    // tagged with 'dontuse' and nothing more. Discovering and validating usable
    // RDMA interfaces across complex network topologies requires systematic
    // topology discovery, which will be implemented in a follow-up change.
    if (ShouldIgnoreDevice(dev_name)) {
      LOG(INFO) << "Ignoring RDMA device " << dev_name << " (marked 'dontuse')";
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
