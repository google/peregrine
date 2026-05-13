
#include "src/internal/engine/engine.h"

#include <cstring>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "src/api/types.h"

namespace peregrine {

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

Engine::Engine(const int num_threads) : stopping_(false) {
  for (int i = 0; i < num_threads; ++i) {
    threads_.emplace_back([this, i]() { workerLoop(i); });
  }
  LOG(INFO) << "ctor, #num_threads = " << threads_.size();
}

bool Engine::hasWork() const { return !reqs_.empty() || stopping_; }

void Engine::workerLoop(const int i) {
  while (true) {
    Entry e = {};
    {  // step 1: get a transport request, if any.
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Engine::hasWork));
      if (reqs_.empty()) {
        DCHECK(stopping_);  // destructor called
        return;
      }
      e = std::move(reqs_.front());
      reqs_.pop_front();
    }

    // step 2: process the request.
    processOne(e.peer, e.req);

    {  // step 3: update the status.
      absl::MutexLock _(mu_);
      DCHECK(sts_.contains(e.handle));
      sts_[e.handle] = Status::kSuccess;
    }
    e = {};
  }
}

Engine::~Engine() {
  LOG(INFO) << "dtor";
  {
    absl::MutexLock _(mu_);
    stopping_ = true;
  }
  // all threads are joined in the threads_ destructor.
}

// A trivial implementation assuming the peer is in the same address space.
void Engine::processOne(const Endpoint peer, const Request& request) {
  switch (request.op) {
    case Op::kRead:  // self <- peer
      std::memcpy(request.laddr, request.raddr, request.len);
      break;
    case Op::kWrite:  // self -> peer
      std::memcpy(request.raddr, request.laddr, request.len);
      break;
    default:
      DCHECK(false) << "Unreachable";
  }
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint peer,
                                       const Request& request) {
  DCHECK(request.IsValid());

  absl::MutexLock _(mu_);
  const Handle handle = genHandle();
  const auto [it, inserted] = sts_.try_emplace(handle, Status::kInProgress);
  if (!inserted) {
    return AlreadyExistsError(handle);
  }

  reqs_.emplace_back(handle, std::string(peer), request);
  return handle;
}

absl::StatusOr<Status> Engine::QueryUpdate(const Handle handle) {
  absl::MutexLock _(mu_);
  const auto it = sts_.find(handle);
  if (it == sts_.end()) {
    return NotFoundError(handle);
  }
  const Status s = it->second;
  if (IsCompleted(s)) {
    sts_.erase(it);
  }
  return s;
}

}  // namespace peregrine
