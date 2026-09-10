
#include "src/internal/engine/engine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "absl/base/optimization.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_metrics.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/engine_helper.h"
#include "src/internal/engine/worker.h"

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

  if ABSL_PREDICT_FALSE (!config.IsValid()) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  auto helper = EngineHelper::Create(config, self, control);
  if ABSL_PREDICT_FALSE (helper == nullptr) {
    LOG(ERROR) << "failed to create engine helper: " << self;
    return nullptr;
  }
  DCHECK(self.IsValid());
  return absl::WrapUnique(new Engine(config, self, std::move(helper)));
}

Engine::Engine(const Config& config, const HostInfo& self,
               std::unique_ptr<EngineHelper> helper)
    : config_(config), self_(self), stop_(false), helper_(std::move(helper)) {
  DCHECK(config_.IsValid());
  DCHECK(self.IsValid());
  DCHECK_NE(helper_, nullptr);

  main_thread_ = std::jthread([this]() { mainLoop(); });
  LOG(INFO) << "engine created @ " << self_;
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    stop_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << "engine destroyed @ " << self_;
}

absl::StatusOr<Handle> Engine::Enqueue(
    const Endpoint& peer, absl::Span<const Request> requests,
    absl::AnyInvocable<void(Status)> on_complete) {
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
    if (helper_->Connect(workers, entry.peer)) {
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

void Engine::GetMetrics(TransportMetrics& m) const {
  metrics_.Snapshot(m);
  helper_->GetMetrics(m);
}

}  // namespace peregrine::internal
