
#include "src/internal/engine/engine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <thread>  // NOLINT
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/engine/worker.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kEngine = "engine ";

absl::Status AlreadyExistsError(const Handle h) {
  return absl::FailedPreconditionError(
      absl::StrFormat("handle 0x%x already exists", h.value()));
}
absl::Status NotFoundError(const Handle h) {
  return absl::NotFoundError(
      absl::StrFormat("handle 0x%x not found", h.value()));
}
}  // namespace

Engine::Engine(std::unique_ptr<TcpAcceptor> acceptor, const HostInfo& self,
               int num_conns_per_peer)
    : self_(self),
      num_conns_per_peer_(num_conns_per_peer),
      stop_(false),
      acceptor_(std::move(acceptor)) {
  DCHECK_GE(num_conns_per_peer_, 1);
  DCHECK_LE(num_conns_per_peer_, 100);

  // Start an acceptor thread.
  DCHECK_NE(acceptor_, nullptr);
  acceptor_thread_ = std::jthread([this]() {
    auto callback = [this](std::unique_ptr<TcpSocket> socket) {
      accept(std::move(socket));
    };
    acceptor_->Start(callback);
  });

  // Start a main loop thread.
  main_thread_ = std::jthread([this]() { mainLoop(); });

  LOG(INFO) << kEngine << "created @ " << self_;
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    acceptor_->Stop();
    stop_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << kEngine << "destroyed @ " << self_;
}

void Engine::accept(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);
  std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
  auto rw = std::make_unique<Worker>(-(1 + recv_workers_.size()), self_,
                                     outgoing_, incoming_, std::move(ch));
  recv_workers_.push_back(std::move(rw));
}

bool Engine::connect(Workers& workers, const Endpoint& peer) {
  if (workers.size() >= num_conns_per_peer_) {
    return true;
  }
  for (int i = 0; i < 2 * num_conns_per_peer_; ++i) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer);
    if (socket == nullptr) continue;
    std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
    auto sw = std::make_unique<Worker>(1 + workers.size(), self_, outgoing_,
                                       incoming_, std::move(ch));
    workers.push_back(std::move(sw));
    if (workers.size() >= num_conns_per_peer_) break;
  }
  return !workers.empty();
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer,
                                       absl::Span<const Request> requests) {
  DCHECK(peer.IsValid());
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

namespace {
uint32_t CalcChunkSize(const size_t len, const uint32_t num_chunks) {
  return static_cast<uint32_t>((len + num_chunks - 1) / num_chunks);
}
}  // namespace

void Engine::processWrite(Workers& workers, const Handle handle,
                          const ReqId reqid, const Request& request) {
  const size_t len = request.len;
  const uint32_t nchunks = std::min(workers.size(), len);
  const uint32_t size = CalcChunkSize(len, nchunks);
  DCHECK_GE(size, 1);
  uint64_t offset = 0;
  for (int i = 0; i < nchunks && offset < len; ++i, offset += size) {
    const Byte* const chunk_src_addr = request.laddr + offset;
    const Byte* const chunk_dst_addr = request.raddr + offset;
    const ChunkMetadata chunk = {
        .handle = handle,
        .reqid = reqid,
        .nchunks = nchunks,
        .index = chunk_t(i),
        .addr = addr_t(reinterpret_cast<uintptr_t>(chunk_dst_addr)),
        .size = i < nchunks - 1 ? size : static_cast<uint32_t>(len - offset),
    };
    workers[i]->EnqueueChunk(chunk_src_addr, chunk);
  }
}

void Engine::processRead(const Handle handle, const ReqId reqid,
                         const Request& request) {
  // TODO(yongx): implement read.
  auto tracker = incoming_.FindOrCreate(handle, reqid, /*num_channels=*/1);
  std::memcpy(request.laddr, request.raddr, request.len);
  tracker->Set(chunk_t(0));
}

}  // namespace peregrine::internal
