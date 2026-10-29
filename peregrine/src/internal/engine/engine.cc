
#include "peregrine/src/internal/engine/engine.h"

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
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/engine/engine_helper.h"
#include "peregrine/src/internal/engine/worker.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

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
                                       Control& control,
                                       RecvPostCallback recv_post_callback) {
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
  DCHECK(self.IsDataPlaneValid());
  return absl::WrapUnique(new Engine(config, self, std::move(helper),
                                     std::move(recv_post_callback)));
}

Engine::Engine(const Config& config, const HostInfo& self,
               std::unique_ptr<EngineHelper> helper,
               RecvPostCallback recv_post_callback)
    : config_(config),
      self_(self),
      stop_(false),
      next_handle_(util::Random<Handle::ValueType>(bitgen_)),
      next_reqid_(util::Random<ReqId::ValueType>(bitgen_)),
      outgoing_(helper->Metrics(), helper->Tracer()),
      incoming_(helper->Metrics(), helper->Tracer(),
                std::move(recv_post_callback)),
      metrics_(helper->Metrics()),
      tracer_(helper->Tracer()),
      helper_(std::move(helper)) {
  DCHECK(config_.IsValid());

  tcpmgr_thread_ = util::Jthread([this]() { tcpmgrLoop(); });
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

void Engine::createSendWorkers(const Endpoint& peer,
                               const TraceContext& trace_ctx,
                               const Handle handle) {
  // TODO(yongx): helper_->Connect(), called at start time, is blocking.
  Workers& workers = send_workers_[peer];
  for (std::unique_ptr<Channel>& ch :
       helper_->Connect(peer, trace_ctx, handle)) {
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

void Engine::tcpmgrLoop() {
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

bool Engine::hasWork() const { return !posts_.empty() || stop_; }

void Engine::mainLoop() {
  while (true) {
    PostEntry post;
    {  // step 1: get a post entry.
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Engine::hasWork));
      if (posts_.empty()) {
        DCHECK(stop_);  // destructor called
        return;
      }
      post = std::move(posts_.front());
      posts_.pop_front();
    }

    // step 2: create send workers if needed.
    if (send_workers_[post.peer].size() < config_.num_conns_per_peer) {
      createSendWorkers(post.peer, post.trace_ctx, post.handle);
    }

    // step 3: process the post.
    process(post);
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
  const TraceContext trace_ctx = item.trace_ctx;

  // TODO(yongx): all the request ops are the same for now.
  RequestTracker& tracker = getRequestTracker(item.requests[0]);
  if (!tracker.Add(handle, item.start_time, std::move(item.on_complete),
                   trace_ctx)) {
    return AlreadyExistsError(handle);
  }
  posts_.push_back(PostEntry{
      .peer = peer,
      .handle = handle,
      .reqs = std::vector<Request>(item.requests.begin(), item.requests.end()),
      .metadata = item.metadata,
      .trace_ctx = trace_ctx,
  });
  tracer_.RecordEngineEnqueued(trace_ctx, handle, item.start_mono_us,
                               item.requests.size(), item.requests[0].op);
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

void Engine::process(const PostEntry& post) {
  DCHECK(!post.reqs.empty());
  if (post.reqs[0].op == Op::kWrite) {
    Workers& workers = send_workers_[post.peer];
    if ABSL_PREDICT_FALSE (workers.empty()) {
      // TODO: Fail or remove `post.handle` in `outgoing_` when error handling
      // is implemented.
      LOG(ERROR) << "failed to create send workers for " << post.peer;
      metrics_.write.errors.Add(1);
    } else {
      processWrite(workers, post);
    }
  } else {
    processRead(post);
  }
}

void Engine::processWrite(Workers& workers, const PostEntry& post) {
  DCHECK(!workers.empty());
  const size_t num_reqs = post.reqs.size();
  std::vector<RequestInfo> req_infos;
  req_infos.reserve(num_reqs);
  for (const Request& req : post.reqs) {
    DCHECK(req.IsValid());
    metrics_.write.request_size_bytes.Record(req.len);
    const ReqId reqid = genReqId();
    tracer_.RecordEngineDequeued(post.trace_ctx, post.handle, reqid, req);
    const uint32_t nchunks = std::min(workers.size(), req.len);
    DCHECK_GE(nchunks, 1);
    req_infos.push_back(RequestInfo{
        .reqid = reqid,
        .nchunks = nchunks,
        .raddr = addr_t(reinterpret_cast<uintptr_t>(req.raddr)),
        .total_len = req.len,
        .op = req.op,
        .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
        .metadata = req.metadata,
    });
  }
  outgoing_.Add(post.handle, req_infos);

  const size_t info_len = num_reqs * kSerializedRequestInfoSize;
  CHECK_LE(info_len, std::numeric_limits<uint32_t>::max());
  PostInfoHeader post_info = {};
  post_info.opcode = Opcode::kPostInfo;
  post_info.handle = post.handle;
  post_info.len = static_cast<uint32_t>(info_len);
  post_info.metadata = post.metadata;
  // TODO: Send `PostInfoHeader` on a single channel once out-of-order
  // `DataHeader` arrival across multiple channels is supported.
  for (auto& worker : workers) {
    worker->EnqueueInfo(post_info, req_infos);
  }

  for (size_t i = 0; i < num_reqs; ++i) {
    processWrite(workers, post.handle, post.reqs[i].laddr, req_infos[i],
                 post.trace_ctx);
  }
}

void Engine::processWrite(Workers& workers, const Handle handle,
                          const Byte* const laddr, const RequestInfo& req_info,
                          const TraceContext& trace_ctx) {
  DCHECK(!workers.empty());
  DCHECK(req_info.IsValid());
  const size_t len = req_info.total_len;
  const uint32_t nchunks = req_info.nchunks;
  DCHECK_GE(nchunks, 1);
  const size_t q = len / nchunks;
  const size_t r = len % nchunks;
  uint64_t offset = 0;
  for (uint32_t i = 0; i < nchunks; ++i) {
    // TODO(yongx): add a queue for chunks if `n` is too big (>= 8 MiB).
    const size_t n = i < r ? (q + 1) : q;
    CHECK_LE(n, std::numeric_limits<uint32_t>::max());  // Crash OK for now
    const uint32_t size = static_cast<uint32_t>(n);
    DataHeader chunk = {};
    chunk.opcode = Opcode::kData;
    chunk.handle = handle;
    chunk.len = size;
    chunk.reqid = req_info.reqid;
    chunk.index = chunk_t(i);
    chunk.byte_offset = offset;
    chunk.raddr = addr_t(req_info.raddr.value() + offset);
    if ABSL_PREDICT_FALSE (trace_ctx.is_traced) {
      chunk.trace.SetTraced();
    }
    offset += size;
    workers[i]->EnqueueChunk(laddr + chunk.byte_offset, chunk);
  }
  DCHECK_EQ(offset, len);
}

void Engine::processRead(const PostEntry& post) {
  // TODO(yongx): implement read.
  for (const Request& req : post.reqs) {
    metrics_.read.request_size_bytes.Record(req.len);
    std::memcpy(req.laddr, req.raddr, req.len);
    const ReqId reqid = genReqId();
    tracer_.RecordEngineDequeued(post.trace_ctx, post.handle, reqid, req);
    incoming_.FindOrCreate(post.handle, reqid, /*num_chunks=*/1);
    incoming_.Update(post.handle, reqid, chunk_t(0), /*worker_id=*/0);
  }
}

}  // namespace peregrine::internal
