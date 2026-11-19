#include "peregrine/src/internal/engine2/engine.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/transfer_manager.h"
#include "peregrine/src/internal/trace/chrome_trace_exporter.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::engine2 {
namespace {

// Returns a random starting value for handle allocation.
Handle::ValueType RandomStartHandle() {
  absl::BitGen bitgen;
  return util::Random<Handle::ValueType>(bitgen);
}

// Returns the next handle from `next_handle`, skipping `kInvalidHandle` when
// the counter wraps around.
Handle GenNextHandle(std::atomic<Handle::ValueType>& next_handle) {
  Handle::ValueType h = next_handle.fetch_add(1, std::memory_order_relaxed) + 1;
  if (h == kInvalidHandle.value()) {
    h = next_handle.fetch_add(1, std::memory_order_relaxed) + 1;
  }
  return Handle(h);
}

}  // namespace

bool Engine::Item::IsValid() const {
  return !requests.empty() &&
         std::all_of(requests.begin(), requests.end(),
                     [](const Request& r) { return r.IsValid(); });
}

std::unique_ptr<Engine> Engine::Create(
    const Config& config, HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn,
    RecvPostCallback recv_post_callback,
    ResolveAddrCallback resolve_addr_callback) {
  if ABSL_PREDICT_FALSE (!config.IsValid()) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  std::unique_ptr<ChannelManager> channel_manager =
      ChannelManager::Create(config, self);
  if ABSL_PREDICT_FALSE (channel_manager == nullptr) {
    LOG(ERROR) << "failed to create channel manager";
    return nullptr;
  }
  std::unique_ptr<TransferManager> transfer_manager =
      TransferManager::Create(config, self, std::move(get_peer_host_info_fn),
                              std::move(channel_manager));
  if ABSL_PREDICT_FALSE (transfer_manager == nullptr) {
    LOG(ERROR) << "failed to create transfer manager";
    return nullptr;
  }
  return absl::WrapUnique(
      new Engine(config, self, std::move(transfer_manager)));
}

Engine::Engine(const Config& config, const HostInfo& self,
               std::unique_ptr<TransferManager> transfer_manager)
    : config_(config),
      self_(self),
      next_handle_(RandomStartHandle()),
      tracer_(TraceConfig{.sample_every_n = 0}),
      transfer_manager_(std::move(transfer_manager)) {}

absl::StatusOr<Handle> Engine::Enqueue(const PeerId& peer, Item item) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(item.IsValid());
  const Handle handle = GenNextHandle(next_handle_);
  transfer_manager_->Submit(
      peer, TransferManager::Post{
                .handle = handle,
                .requests = std::vector<Request>(item.requests.begin(),
                                                 item.requests.end()),
                .metadata = item.metadata,
                .trace_ctx = item.trace_ctx,
            });
  return handle;
}

absl::StatusOr<Status> Engine::QueryUpdate(Handle handle) {
  return absl::UnimplementedError("Engine::QueryUpdate");
}

absl::Status Engine::RegisterMemory(void* addr, size_t length) {
  return absl::OkStatus();
}

absl::Status Engine::UnregisterMemory(const void* addr) {
  return absl::OkStatus();
}

std::string Engine::ExportChromeTraceJson() const {
  static_assert(
      ::peregrine::assumptions::kRulesToFollowWhenAddingNewTraceEvents);
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);
  return ChromeTraceExporter::Export(tracer_.Buffer().Snapshot(),
                                     self_.control_plane_listener.ToString());
}

}  // namespace peregrine::internal::engine2
