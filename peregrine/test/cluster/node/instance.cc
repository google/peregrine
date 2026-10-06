#include "peregrine/test/cluster/node/instance.h"

#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/api/transport_util.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {
namespace {

constexpr int kMaxCreateThreads = 32;
constexpr int kMaxDestroyThreads = 64;
constexpr int kMaxPortBindAttempts = 128;

std::string FormatEndpoint(std::string_view ip, uint16_t port) {
  if (absl::StrContains(ip, ':')) {
    return absl::StrFormat("[%s]:%u", ip, port);
  }
  return absl::StrFormat("%s:%u", ip, port);
}

// Executes `body(idx)` for `idx` in `[0, total_items)` across up to
// `max_threads` worker threads, stopping early on the first non-OK status.
absl::Status ParallelFor(int total_items, int max_threads,
                         absl::FunctionRef<absl::Status(int)> body) {
  if (total_items <= 0) {
    return absl::OkStatus();
  }
  const int num_threads = std::min(total_items, std::max(1, max_threads));
  std::atomic<int> next_index{0};
  absl::Mutex err_mu;
  absl::Status first_error = absl::OkStatus();

  std::vector<util::Thread> workers;
  workers.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    workers.emplace_back([&]() {
      while (true) {
        const int idx = next_index.fetch_add(1, std::memory_order_relaxed);
        if (idx >= total_items) {
          break;
        }
        {
          absl::MutexLock lock(err_mu);
          if (!first_error.ok()) break;
        }
        absl::Status s = body(idx);
        if (!s.ok()) {
          absl::MutexLock lock(err_mu);
          if (first_error.ok()) {
            first_error = std::move(s);
          }
          break;
        }
      }
    });
  }
  for (util::Thread& w : workers) {
    w.join();
  }
  return first_error;
}

absl::StatusOr<std::pair<std::string, std::unique_ptr<Transport>>>
BindTransportWithRetry(int idx, const NodeConfig& config,
                       std::atomic<uint32_t>& next_port) {
  for (int attempt = 0; attempt < kMaxPortBindAttempts; ++attempt) {
    const uint32_t candidate =
        next_port.fetch_add(1, std::memory_order_relaxed);
    if (candidate > 65535U) {
      break;
    }
    std::string endpoint =
        FormatEndpoint(config.ip, static_cast<uint16_t>(candidate));
    std::unique_ptr<Transport> transport =
        CreateTransport(endpoint, config.transport_type, config.num_conns);
    if (transport != nullptr) {
      return std::make_pair(std::move(endpoint), std::move(transport));
    }
  }
  return absl::InternalError(absl::StrCat(
      "Failed to bind Transport instance ", idx, " on IP ", config.ip,
      " starting from port ", config.base_control_port));
}

absl::StatusOr<std::unique_ptr<NodeInstance>> CreateSingleInstance(
    int idx, const NodeConfig& config, RegisteredBuffer* shared_buf,
    size_t buf_size, std::atomic<uint32_t>& next_port) {
  auto bound_or = BindTransportWithRetry(idx, config, next_port);
  if (!bound_or.ok()) {
    return bound_or.status();
  }
  auto [bound_endpoint, transport] = std::move(*bound_or);

  std::unique_ptr<RegisteredBuffer> owned_buf;
  RegisteredBuffer* target_buf = shared_buf;
  if (target_buf == nullptr) {
    owned_buf = std::make_unique<RegisteredBuffer>(buf_size);
    target_buf = owned_buf.get();
  }

  absl::Status reg_status = target_buf->RegisterWith(transport.get());
  if (!reg_status.ok()) {
    return reg_status;
  }

  return std::make_unique<NodeInstance>(std::move(bound_endpoint),
                                        std::move(transport), shared_buf,
                                        std::move(owned_buf));
}

}  // namespace

absl::Status RaiseNoFileLimit(uint64_t target_nofile) {
  struct rlimit rl{};
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    return absl::InternalError(absl::StrCat("getrlimit(RLIMIT_NOFILE) failed: ",
                                            std::strerror(errno)));
  }
  const rlim_t desired =
      (rl.rlim_max == RLIM_INFINITY)
          ? static_cast<rlim_t>(target_nofile)
          : std::min<rlim_t>(static_cast<rlim_t>(target_nofile), rl.rlim_max);
  if (rl.rlim_cur < desired) {
    rl.rlim_cur = desired;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
      return absl::InternalError(absl::StrCat(
          "setrlimit(RLIMIT_NOFILE) failed: ", std::strerror(errno)));
    }
  }
  return absl::OkStatus();
}

RegisteredBuffer::RegisteredBuffer(size_t size_bytes, bool zero_fill)
    : data_(std::max<size_t>(1, size_bytes), 0) {
  if (!zero_fill) {
    util::RandomNonZero(absl::MakeSpan(data_));
  }
}

RegisteredBuffer::~RegisteredBuffer() { UnregisterAll(); }

absl::Status RegisteredBuffer::RegisterWith(Transport* transport) {
  CHECK(transport != nullptr);
  absl::Status s = transport->RegisterMemory(data_.data(), data_.size());
  if (!s.ok()) {
    return s;
  }
  absl::MutexLock lock(mu_);
  registered_transports_.push_back(transport);
  return absl::OkStatus();
}

void RegisteredBuffer::UnregisterFrom(Transport* transport) {
  absl::MutexLock lock(mu_);
  auto it = std::find(registered_transports_.begin(),
                      registered_transports_.end(), transport);
  if (it != registered_transports_.end()) {
    (*it)->UnregisterMemory(data_.data()).IgnoreError();
    registered_transports_.erase(it);
  }
}

void RegisteredBuffer::UnregisterAll() {
  std::vector<Transport*> to_unregister;
  {
    absl::MutexLock lock(mu_);
    to_unregister.swap(registered_transports_);
  }
  for (Transport* t : to_unregister) {
    t->UnregisterMemory(data_.data()).IgnoreError();
  }
}

NodeInstance::NodeInstance(std::string control_endpoint,
                           std::unique_ptr<Transport> transport,
                           RegisteredBuffer* shared_buffer,
                           std::unique_ptr<RegisteredBuffer> owned_buffer)
    : control_endpoint_(std::move(control_endpoint)),
      active_buffer_(shared_buffer != nullptr ? shared_buffer
                                              : owned_buffer.get()),
      owned_buffer_(std::move(owned_buffer)),
      transport_(std::move(transport)) {
  CHECK(transport_ != nullptr);
  CHECK(active_buffer_ != nullptr);
}

NodeInstance::~NodeInstance() {
  if (active_buffer_ != nullptr && transport_ != nullptr) {
    active_buffer_->UnregisterFrom(transport_.get());
  }
  transport_.reset();
  owned_buffer_.reset();
}

absl::StatusOr<Handle> NodeInstance::PostAsync(
    std::string_view peer, absl::Span<const Request> requests,
    absl::AnyInvocable<void(Status)> on_complete) {
  return transport_->Post(peer, requests, std::move(on_complete));
}

absl::StatusOr<std::vector<std::unique_ptr<NodeInstance>>> CreateInstances(
    const NodeConfig& config, RegisteredBuffer* shared_buf, size_t buf_size) {
  if (config.num_instances <= 0) {
    return absl::InvalidArgumentError("num_instances must be > 0");
  }

  std::vector<std::unique_ptr<NodeInstance>> instances(config.num_instances);
  std::atomic<uint32_t> next_port{
      config.base_control_port > 0 ? config.base_control_port : 10000U};

  absl::Status status =
      ParallelFor(config.num_instances, kMaxCreateThreads, [&](int idx) {
        auto inst_or =
            CreateSingleInstance(idx, config, shared_buf, buf_size, next_port);
        if (!inst_or.ok()) {
          return inst_or.status();
        }
        instances[idx] = std::move(*inst_or);
        return absl::OkStatus();
      });

  if (!status.ok()) {
    DestroyInstances(instances);
    return status;
  }
  return instances;
}

void DestroyInstances(std::vector<std::unique_ptr<NodeInstance>>& instances) {
  ParallelFor(static_cast<int>(instances.size()), kMaxDestroyThreads,
              [&](int idx) {
                instances[idx].reset();
                return absl::OkStatus();
              })
      .IgnoreError();
  instances.clear();
}

}  // namespace peregrine::cluster
