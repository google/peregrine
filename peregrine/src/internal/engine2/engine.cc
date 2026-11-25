#include "peregrine/src/internal/engine2/engine.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/engine2/transfer_manager.h"
#include "peregrine/src/internal/trace/chrome_trace_exporter.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::engine2 {
namespace {

template <typename T>
T RandomStart() {
  absl::BitGen bitgen;
  return util::Random<T>(bitgen);
}

// Returns the next handle, skipping `kInvalidHandle` on wrap-around.
Handle GenNextHandle(std::atomic<Handle::ValueType>& next_handle) {
  Handle::ValueType h = next_handle.fetch_add(1, std::memory_order_relaxed) + 1;
  if (h == kInvalidHandle.value()) {
    h = next_handle.fetch_add(1, std::memory_order_relaxed) + 1;
  }
  return Handle(h);
}

// Returns the next request ID, skipping `kInvalidReqId` on wrap-around.
ReqId GenNextReqId(std::atomic<ReqId::ValueType>& next_req_id) {
  ReqId::ValueType id = next_req_id.fetch_add(1, std::memory_order_relaxed) + 1;
  if (id == kInvalidReqId.value()) {
    id = next_req_id.fetch_add(1, std::memory_order_relaxed) + 1;
  }
  return ReqId(id);
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
  if (ABSL_PREDICT_FALSE(!config.IsValid())) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(config.require_dataplane_encryption)) {
    LOG(ERROR) << "dataplane encryption is not supported";
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(config.transport_type == TransportType::kRdma)) {
    LOG(ERROR) << "RDMA transport is not supported";
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(get_peer_host_info_fn == nullptr)) {
    LOG(ERROR) << "null peer host info lookup";
    return nullptr;
  }

  std::unique_ptr<ChannelManager> channel_manager =
      ChannelManager::Create(config, self);
  if (ABSL_PREDICT_FALSE(channel_manager == nullptr)) {
    LOG(ERROR) << "failed to create channel manager";
    return nullptr;
  }

  auto engine =
      absl::WrapUnique(new Engine(config, self, std::move(recv_post_callback),
                                  std::move(resolve_addr_callback)));
  engine->transfer_manager_ = TransferManager::Create(
      config, self, engine->outgoing_, engine->incoming_,
      std::move(get_peer_host_info_fn), std::move(channel_manager));
  if (ABSL_PREDICT_FALSE(engine->transfer_manager_ == nullptr)) {
    LOG(ERROR) << "failed to create transfer manager";
    return nullptr;
  }
  return engine;
}

Engine::Engine(const Config& config, const HostInfo& self,
               RecvPostCallback recv_post_callback,
               ResolveAddrCallback resolve_addr_callback)
    : config_(config),
      self_(self),
      next_handle_(RandomStart<Handle::ValueType>()),
      next_req_id_(RandomStart<ReqId::ValueType>()),
      tracer_(TraceConfig{.sample_every_n = 0}),
      outgoing_(metrics_, tracer_),
      incoming_(metrics_, tracer_, std::move(recv_post_callback),
                std::move(resolve_addr_callback)) {}

absl::StatusOr<Handle> Engine::Enqueue(const PeerId& peer, Item item) {
  if (ABSL_PREDICT_FALSE(!peer.HasNonzeroIpPort())) {
    return absl::InvalidArgumentError("invalid peer endpoint");
  }
  if (ABSL_PREDICT_FALSE(!item.IsValid())) {
    return absl::InvalidArgumentError("invalid post item");
  }

  std::vector<RequestInfo> req_infos;
  req_infos.reserve(item.requests.size());
  for (const Request& req : item.requests) {
    if (ABSL_PREDICT_FALSE(req.op != Op::kWrite)) {
      return absl::UnimplementedError("only write requests are supported");
    }
    const uint64_t nchunks = (req.len - 1) / kChunkSize + 1;
    if (ABSL_PREDICT_FALSE(nchunks > RequestInfo::kMaxChunksPerReq)) {
      return absl::InvalidArgumentError("request exceeds max chunk count");
    }
    req_infos.push_back(RequestInfo{
        .reqid = GenNextReqId(next_req_id_),
        .nchunks = static_cast<uint32_t>(nchunks),
        .raddr = addr_t(reinterpret_cast<uintptr_t>(req.raddr)),
        .total_len = req.len,
        .op = req.op,
        .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
        .metadata = req.metadata,
    });
  }

  constexpr int kMaxHandleAttempts = 16;
  Handle handle = kInvalidHandle;
  bool added_handle = false;
  for (int attempt = 0; attempt < kMaxHandleAttempts; ++attempt) {
    handle = GenNextHandle(next_handle_);
    if (outgoing_.Add(handle, item.start_mono_us, std::move(item.on_complete),
                      item.trace_ctx)) {
      added_handle = true;
      break;
    }
  }
  if (ABSL_PREDICT_FALSE(!added_handle)) {
    return absl::ResourceExhaustedError("failed to register handle");
  }
  if (ABSL_PREDICT_FALSE(
          !outgoing_.AddInitiatorReqs(handle, req_infos, peer))) {
    outgoing_.Remove(handle);
    return absl::InternalError("failed to register requests for handle");
  }

  transfer_manager_->Submit(peer,
                            Post{
                                .handle = handle,
                                .requests = std::vector<Request>(
                                    item.requests.begin(), item.requests.end()),
                                .metadata = item.metadata,
                                .trace_ctx = item.trace_ctx,
                            },
                            std::move(req_infos));
  return handle;
}

absl::StatusOr<Status> Engine::QueryUpdate(const Handle handle) {
  for (RequestTracker* tracker : {&outgoing_, &incoming_}) {
    const Status s = tracker->Check(handle);
    if (IsInProgress(s)) return s;
    if (IsCompleted(s)) {
      tracker->Remove(handle);
      return s;
    }
  }
  return absl::NotFoundError(absl::StrCat("unknown handle ", handle.value()));
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
