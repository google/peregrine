
#include "src/internal/engine/engine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/engine_helper.h"
#include "src/internal/engine/worker.h"
#include "src/internal/metrics/engine_metrics.h"
#include "src/internal/request/request_tracker.h"
#include "src/util/thread.h"

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
    : config_(config),
      self_(self),
      stop_(false),
      metrics_(helper->Metrics()),
      helper_(std::move(helper)) {
  DCHECK(config_.IsValid());

  acceptor_thread_ = util::Jthread([this]() { acceptorLoop(); });
  main_thread_ = util::Jthread([this]() { mainLoop(); });
  LOG(INFO) << "created @ " << self_;
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    stop_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << "engine destroyed @ " << self_;
}

void Engine::createSendWorkers(const Endpoint& peer) {
  // TODO(yongx): helper_->Connect(), called at start time, is blocking.
  Workers& workers = send_workers_[peer];
  for (std::unique_ptr<Channel>& ch : helper_->Connect(peer)) {
    auto sw = createWorker(1 + workers.size(), std::move(ch));
    workers.push_back(std::move(sw));
  }
}

bool Engine::createRecvWorkers() {
  auto channels = helper_->GetAcceptedChannels();
  if (channels.empty()) {
    return false;
  }
  // TODO(yongx): use peer control endpoint as key.
  Workers& workers = recv_workers_[Endpoint()];
  for (auto& ch : channels) {
    auto rw = createWorker(-(1 + workers.size()), std::move(ch));
    workers.push_back(std::move(rw));
  }
  return true;
}

void Engine::acceptorLoop() {
  while (true) {
    {
      absl::MutexLock _(mu_);
      if (stop_) return;
    }
    if (!createRecvWorkers()) {
      absl::SleepFor(absl::Milliseconds(100));
    }
  }
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

    // step 2: create send workers if needed.
    if (send_workers_[entry.peer].size() < config_.num_conns_per_peer) {
      createSendWorkers(entry.peer);
    }

    // step 3: process the entry.
    process(entry);
  }
}

bool Engine::Item::IsValid() const {
  return !requests.empty() &&
         std::all_of(requests.begin(), requests.end(),
                     [](const Request& r) { return r.IsValid(); });
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer, Item item) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(item.IsValid());

  absl::MutexLock _(mu_);
  const Handle handle = genHandle();

  std::vector<ReqId> reqids;
  const auto& requests = item.requests;
  reqids.reserve(requests.size());
  for (int i = 0; i < requests.size(); ++i) {
    reqids.push_back(genReqId());
  }
  DCHECK_EQ(reqids.size(), requests.size());

  // TODO(yongx): all the request ops are the same for now.
  RequestTracker& tracker = getRequestTracker(requests[0]);
  if (!tracker.Add(handle, reqids, std::move(item.on_complete))) {
    return AlreadyExistsError(handle);
  }
  for (int i = 0; i < requests.size(); ++i) {
    reqs_.emplace_back(peer, handle, reqids[i], requests[i]);
  }
  metrics_.requests_posted.Add(requests.size());
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

void Engine::process(const Entry& entry) {
  if (entry.request.op == Op::kWrite) {
    Workers& workers = send_workers_[entry.peer];
    if ABSL_PREDICT_FALSE (workers.empty()) {
      LOG(ERROR) << "failed to create send workers for " << entry.peer;
      metrics_.e2e_write_errors.Add(1);
    } else {
      processWrite(workers, entry.handle, entry.reqid, entry.request);
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
  std::memcpy(request.laddr, request.raddr, request.len);
  incoming_.Update(handle, reqid, /*num_chunks=*/1, chunk_t(0));
}

}  // namespace peregrine::internal
