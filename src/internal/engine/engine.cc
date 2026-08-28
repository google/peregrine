
#include "src/internal/engine/engine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/worker.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/internal/rdma/rdma_device_manager.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

namespace {
absl::Status AlreadyExistsError(const Handle h) {
  return absl::FailedPreconditionError(
      absl::StrFormat("handle 0x%x already exists", h.value()));
}
absl::Status NotFoundError(const Handle h) {
  return absl::NotFoundError(
      absl::StrFormat("handle 0x%x not found", h.value()));
}
}  // namespace

std::unique_ptr<Engine> Engine::Create(const Config& config, HostInfo& self,
                                       Control& control) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  std::unique_ptr<TcpAcceptor> acceptor = nullptr;
  std::unique_ptr<RdmaDeviceManager> rdma_device_manager = nullptr;

  if (config.transport_type == TransportType::kTcp) {
    acceptor = TcpAcceptor::Create(self);
    if ABSL_PREDICT_FALSE (acceptor == nullptr) {
      LOG(WARNING) << "failed to create acceptor: " << self;
      return nullptr;
    }
  } else if (config.transport_type == TransportType::kRdma) {
    auto rdma_mgr_or = RdmaDeviceManager::Create();
    if ABSL_PREDICT_FALSE (!rdma_mgr_or.ok()) {
      LOG(WARNING) << "failed to create rdma device manager: "
                   << rdma_mgr_or.status();
      return nullptr;
    }
    rdma_device_manager = std::move(*rdma_mgr_or);
    for (const auto& dev : rdma_device_manager->Devices()) {
      self.rdma_interfaces.push_back({
          .name = std::string(dev->Name()),
          .gid = std::string(reinterpret_cast<const char*>(dev->LocalGid().raw),
                             sizeof(dev->LocalGid().raw)),
          .port_num = RdmaDeviceContext::kDefaultPortNum,
      });
    }
    if ABSL_PREDICT_FALSE (self.rdma_interfaces.empty()) {
      LOG(WARNING) << "no active RDMA devices found: " << self;
      return nullptr;
    }
  } else {
    return nullptr;
  }

  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(WARNING) << "invalid self host info: " << self;
    return nullptr;
  }
  return absl::WrapUnique(new Engine(config, self, std::move(acceptor),
                                     std::move(rdma_device_manager), control));
}

Engine::Engine(const Config& config, HostInfo& self,
               std::unique_ptr<TcpAcceptor> acceptor,
               std::unique_ptr<RdmaDeviceManager> rdma_device_manager,
               Control& control)
    : config_(config),
      self_(self),
      control_(control),
      stop_(false),
      acceptor_(std::move(acceptor)),
      rdma_device_manager_(std::move(rdma_device_manager)) {
  DCHECK(config_.IsValid());

  // Start an acceptor thread if TCP acceptor is present.
  if (acceptor_ != nullptr) {
    acceptor_thread_ = std::jthread([this]() {
      auto callback = [this](std::unique_ptr<TcpSocket> socket) {
        accept(std::move(socket));
      };
      acceptor_->Start(callback);
    });
  }

  // Start a main loop thread.
  main_thread_ = std::jthread([this]() { mainLoop(); });

  LOG(INFO) << "created @ " << self_;
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    if (acceptor_ != nullptr) {
      acceptor_->Stop();
    }
    stop_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << "destroyed @ " << self_;
}

void Engine::accept(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);
  std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
  auto rw = std::make_unique<Worker>(-(1 + recv_workers_.size()), self_,
                                     outgoing_, incoming_, std::move(ch));
  recv_workers_.push_back(std::move(rw));
}

bool Engine::connect(Workers& workers, const Endpoint& peer) {
  const int num_conns = config_.num_conns_per_peer;
  if (workers.size() >= num_conns) {
    return true;
  }
  auto host_info = control_.GetPeerHostInfo(peer);
  if (!host_info.ok()) {
    LOG(WARNING) << "failed to resolve peer " << peer << ": "
                 << host_info.status();
    return false;
  }
  if (host_info->data_plane_listeners.empty()) {
    LOG(WARNING) << "no data plane listeners found for peer " << peer;
    return false;
  }
  // We only use the first data plane listener for now.
  const Endpoint& target = host_info->data_plane_listeners[0];

  for (int i = 0; i < 2 * num_conns; ++i) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(target);
    if (socket == nullptr) continue;
    std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
    auto sw = std::make_unique<Worker>(1 + workers.size(), self_, outgoing_,
                                       incoming_, std::move(ch));
    workers.push_back(std::move(sw));
    if (workers.size() >= num_conns) break;
  }
  return !workers.empty();
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer,
                                       absl::Span<const Request> requests) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(IsValid(requests));

  absl::MutexLock _(mu_);
  const Handle handle = genHandle();
  // TODO(yongx): all the request ops are the same for now.
  if (!getRequestTracker(requests[0]).Add(handle)) {
    return AlreadyExistsError(handle);
  }
  for (const auto& request : requests) {
    const ReqId reqid = genReqId();
    reqs_.emplace_back(peer, handle, reqid, request);
  }
  return handle;
}

absl::StatusOr<Status> Engine::QueryUpdate(const Handle handle) {
  for (auto* tracker : {&outgoing_, &incoming_}) {
    if (const Status s = tracker->Check(handle); IsInProgress(s)) {
      return s;
    } else if (IsCompleted(s)) {
      tracker->Remove(handle);
      return s;
    }
  }
  return NotFoundError(handle);
}

bool Engine::hasWork() const { return !reqs_.empty() || stop_; }

void Engine::mainLoop() {
  while (true) {
    Entry entry;
    {  // step 1: get an entry.
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Engine::hasWork));
      if (reqs_.empty()) {
        DCHECK(stop_);  // destructor called
        return;
      }
      entry = std::move(reqs_.front());
      reqs_.pop_front();
    }

    // step 2: update the request tracker.
    getRequestTracker(entry.request).Add(entry.handle);

    // step 3: process the entry.
    process(entry);
  }
}

void Engine::process(const Entry& entry) {
  if (entry.request.op == Op::kWrite) {
    Workers& workers = send_workers_[entry.peer];
    if (connect(workers, entry.peer)) {
      processWrite(workers, entry.handle, entry.reqid, entry.request);
    } else {
      // TODO(yongx): handle failure.
    }
  } else {
    processRead(entry.handle, entry.reqid, entry.request);
  }
}

void Engine::processWrite(Workers& workers, const Handle handle,
                          const ReqId reqid, const Request& request) {
  DCHECK(!workers.empty());
  DCHECK(request.IsValid());
  const size_t len = request.len;
  const uint32_t nchunks = std::min(workers.size(), len);
  DCHECK_GE(nchunks, 1);
  const size_t q = len / nchunks;
  const size_t r = len % nchunks;
  uint64_t offset = 0;
  for (uint32_t i = 0; i < nchunks; ++i) {
    const Byte* const chunk_src_addr = request.laddr + offset;
    const Byte* const chunk_dst_addr = request.raddr + offset;
    // TODO(yongx): add a queue for chunks if `n` is too big (>= 8 MiB).
    const size_t n = i < r ? (q + 1) : q;
    CHECK_LE(n, std::numeric_limits<uint32_t>::max());  // Crash OK for now
    const uint32_t size = static_cast<uint32_t>(n);
    offset += size;
    const ChunkHeader chunk = {
        .handle = handle,
        .reqid = reqid,
        .nchunks = nchunks,
        .index = chunk_t(i),
        .addr = addr_t(reinterpret_cast<uintptr_t>(chunk_dst_addr)),
        .size = size,
    };
    workers[i]->EnqueueChunk(chunk_src_addr, chunk);
  }
  DCHECK_EQ(offset, len);
}

void Engine::processRead(const Handle handle, const ReqId reqid,
                         const Request& request) {
  // TODO(yongx): implement read.
  auto tracker = incoming_.FindOrCreate(handle, reqid, /*num_channels=*/1);
  std::memcpy(request.laddr, request.raddr, request.len);
  tracker->Set(chunk_t(0));
}

}  // namespace peregrine::internal
