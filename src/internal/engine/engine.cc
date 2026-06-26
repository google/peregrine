
#include "src/internal/engine/engine.h"

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
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
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

  LOG(INFO) << kEngine << "created @ " << this;
}

Engine::~Engine() {
  {
    absl::MutexLock _(mu_);
    acceptor_->Stop();
    stopping_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << kEngine << "destroyed @ " << this;
}

void Engine::accept(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);
  std::unique_ptr<Channel> channel = CreateTcpChannel(std::move(socket));
  addWorker(std::move(channel));
}

void Engine::connect(const Endpoint& peer, int num_channels) {
  for (int i = 0; i < 2 * num_channels; ++i) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer);
    if (socket == nullptr) continue;
    std::unique_ptr<Channel> channel = CreateTcpChannel(std::move(socket));
    addWorker(std::move(channel));
    if (workers_.size() >= num_channels) break;
  }
}

void Engine::addWorker(std::unique_ptr<Channel> channel) {
  auto worker = std::make_unique<Worker>(send_, recv_, std::move(channel));
  workers_.push_back(std::move(worker));
}

absl::StatusOr<Handle> Engine::Enqueue(const Endpoint& peer,
                                       const Request& request) {
  DCHECK(request.IsValid());

  Handle handle;
  Buffer buffer;
  {
    absl::MutexLock _(mu_);
    handle = genHandle();
    buffer = genBuffer();
  }

  if (request.op == Op::kWrite) {
    if (!send_.Add(handle)) return AlreadyExistsError(handle);
  } else {
    DCHECK_EQ(request.op, Op::kRead);
    if (!recv_.Add(handle)) return AlreadyExistsError(handle);
  }

  {
    absl::MutexLock _(mu_);
    reqs_.emplace_back(peer, handle, buffer, request);
  }
  return handle;
}

absl::StatusOr<Status> Engine::QueryUpdate(const Handle handle) {
  if (send_.Contains(handle)) {
    if (send_.IsDone(handle)) {
      send_.Remove(handle);
      return Status::kSuccess;
    } else {
      return Status::kInProgress;
    }
  }
  if (recv_.Contains(handle)) {
    if (recv_.IsDone(handle)) {
      recv_.Remove(handle);
      return Status::kSuccess;
    } else {
      return Status::kInProgress;
    }
  }
  return NotFoundError(handle);
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

    // step 2: update the buffer tracker.
    if (entry.req.op == Op::kWrite) {
      send_.Add(entry.handle);
    } else {
      recv_.Add(entry.handle);
    }

    // step 3: process the entry.
    process(entry);
  }
}

void Engine::process(const Entry& entry) {
  const Request& req = entry.req;
  if (req.op == Op::kWrite) {
    const int kNumChannels = 1;
    connect(entry.peer, kNumChannels);
    if (workers_.empty()) {
      // TODO(yongx): handle error by failing the request.
      return;
    }
    processWrite(entry.handle, entry.buffer, req);
  } else {
    processRead(entry.handle, entry.buffer, req);
  }
}

namespace {
uint32_t CalcChunkSize(const size_t len, const uint32_t num_chunks) {
  DCHECK_GE(num_chunks, 1);
  return static_cast<uint32_t>((len + num_chunks - 1) / num_chunks);
}
}  // namespace

void Engine::processWrite(const Handle handle, const Buffer buffer,
                          const Request& request) {
  const size_t len = request.len;
  const uint32_t nchunks = workers_.size();
  const uint32_t size = CalcChunkSize(len, nchunks);
  uint64_t offset = 0;
  for (int i = 0; i < nchunks; ++i, offset += size) {
    const Byte* const chunk_src_addr = request.laddr + offset;
    const Byte* const chunk_dst_addr = request.raddr + offset;
    const ChunkMetadata chunk = {
        .handle = handle,
        .buffer = buffer,
        .nchunks = nchunks,
        .index = chunk_t(i),
        .addr = addr_t(reinterpret_cast<uintptr_t>(chunk_dst_addr)),
        .size = i < nchunks - 1 ? size : static_cast<uint32_t>(len - offset),
    };
    workers_[i]->SendChunk(chunk_src_addr, chunk);
  }
}

void Engine::processRead(const Handle handle, const Buffer buffer,
                         const Request& request) {
  // TODO(yongx): implement read.
  constexpr uint32_t kNumChunks = 1;
  auto tracker = recv_.FindOrCreate(handle, buffer, kNumChunks);
  std::memcpy(request.laddr, request.raddr, request.len);
  tracker->Set(chunk_t(0));
}

}  // namespace peregrine::internal
