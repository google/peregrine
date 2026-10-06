#ifndef PEREGRINE_TEST_CLUSTER_NODE_INSTANCE_H_
#define PEREGRINE_TEST_CLUSTER_NODE_INSTANCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {

// Raises the soft file descriptor limit (RLIMIT_NOFILE) up to
// min(target_nofile, rlim_max) to support high instance/connection counts.
absl::Status RaiseNoFileLimit(uint64_t target_nofile = 1048576);

// RAII memory buffer that tracks Transport registrations and unregisters memory
// cleanly before underlying Transport instances are destroyed.
class RegisteredBuffer {
 public:
  explicit RegisteredBuffer(size_t size_bytes, bool zero_fill = false);
  ~RegisteredBuffer();

  RegisteredBuffer(const RegisteredBuffer&) = delete;
  RegisteredBuffer& operator=(const RegisteredBuffer&) = delete;

  Byte* DataPtr() { return data_.data(); }
  const Byte* DataPtr() const { return data_.data(); }
  size_t Size() const { return data_.size(); }
  uint64_t AddrUint64() const {
    return reinterpret_cast<uint64_t>(data_.data());
  }

  absl::Status RegisterWith(Transport* transport);
  void UnregisterFrom(Transport* transport);
  void UnregisterAll();

 private:
  std::vector<Byte> data_;
  absl::Mutex mu_;
  std::vector<Transport*> registered_transports_ ABSL_GUARDED_BY(mu_);
};

// Wraps a single Peregrine `Transport` instance, its bound control endpoint,
// and its associated registered memory buffer.
class NodeInstance {
 public:
  NodeInstance(std::string control_endpoint,
               std::unique_ptr<Transport> transport,
               RegisteredBuffer* shared_buffer,
               std::unique_ptr<RegisteredBuffer> owned_buffer = nullptr);
  ~NodeInstance();

  NodeInstance(const NodeInstance&) = delete;
  NodeInstance& operator=(const NodeInstance&) = delete;

  const std::string& ControlEndpoint() const { return control_endpoint_; }
  Transport& GetTransport() const { return *transport_; }
  Byte* DataPtr() const { return active_buffer_->DataPtr(); }
  size_t DataSize() const { return active_buffer_->Size(); }
  uint64_t BufferAddr() const { return active_buffer_->AddrUint64(); }

  absl::StatusOr<Handle> PostAsync(
      std::string_view peer, absl::Span<const Request> requests,
      absl::AnyInvocable<void(Status)> on_complete);

  TransportMetrics GetTransportMetrics() const {
    return transport_->GetTransportMetrics();
  }

 private:
  std::string control_endpoint_;
  RegisteredBuffer* active_buffer_ = nullptr;
  std::unique_ptr<RegisteredBuffer> owned_buffer_;
  std::unique_ptr<Transport> transport_;
};

// Creates `config.num_instances` Peregrine `NodeInstance` objects in parallel
// using deterministic atomic port allocation starting at
// `config.base_control_port`.
absl::StatusOr<std::vector<std::unique_ptr<NodeInstance>>> CreateInstances(
    const NodeConfig& config, RegisteredBuffer* shared_buf, size_t buf_size);

// Destroys all `NodeInstance` objects in parallel over a bounded thread pool
// so per-instance Engine thread join waits overlap rather than serializing.
void DestroyInstances(std::vector<std::unique_ptr<NodeInstance>>& instances);

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_INSTANCE_H_
