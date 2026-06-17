
#include "src/internal/engine/engine.h"

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
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/acceptor.h"
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

Engine::Engine(std::unique_ptr<TcpAcceptor> acceptor)
    : stopping_(false), acceptor_(std::move(acceptor)) {
  // Start an acceptor thread.
  DCHECK(acceptor_ != nullptr);
  acceptor_thread_ = std::jthread([this]() {
    auto callback = [this](std::unique_ptr<TcpSocket> socket) {
      accept(std::move(socket));
    };
    acceptor_->Start(callback);
  });

  // Start a main loop thread.
  main_thread_ = std::jthread([this]() { mainLoop(); });

  LOG(INFO) << kEngine << "created";
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    acceptor_->Stop();
    stopping_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << kEngine << "destroyed";
}

void Engine::accept(std::unique_ptr<TcpSocket> socket) {
  auto _ = std::move(socket);
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer,
                                       const Request& request) {
  DCHECK(request.IsValid());

  absl::MutexLock _(mu_);
  const Handle handle = genHandle();
  const auto [it, inserted] = sts_.try_emplace(handle, Status::kInProgress);
  if (!inserted) {
    return AlreadyExistsError(handle);
  }

  reqs_.emplace_back(handle, peer, request);
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

bool Engine::hasWork() const { return !reqs_.empty() || stopping_; }

void Engine::mainLoop() {
  while (true) {
    Entry entry;
    {  // step 1: get an entry.
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Engine::hasWork));
      if (reqs_.empty()) {
        DCHECK(stopping_);  // destructor called
        return;
      }
      entry = std::move(reqs_.front());
      reqs_.pop_front();
    }

    // step 2: process the entry.
    processOne(entry);
  }
}

void Engine::processOne(const Entry& entry) {
  process(entry.peer, entry.req);

  absl::MutexLock _(mu_);
  auto it = sts_.find(entry.handle);
  DCHECK_NE(it, sts_.end());
  it->second = Status::kSuccess;
}

// A trivial implementation assuming the peer is in the same address space.
void Engine::process(const Endpoint& peer, const Request& request) {
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

}  // namespace peregrine::internal
