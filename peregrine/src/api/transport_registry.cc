#include "peregrine/src/api/transport_registry.h"

#include <memory>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace peregrine {
namespace {

// Holds the registered RDMA transport factory. Once set it is never reset, so
// `GetRegisteredRdmaTransport()` can hand out a pointer that outlives the lock.
class RdmaTransportSlot final {
 public:
  static RdmaTransportSlot& Get() {
    static absl::NoDestructor<RdmaTransportSlot> slot;
    return *slot;
  }

  absl::Status Set(TransportFactory factory) ABSL_LOCKS_EXCLUDED(mu_) {
    if (factory == nullptr) {
      return absl::InvalidArgumentError("Null RDMA transport factory");
    }
    absl::MutexLock lock(mu_);
    if (factory_ != nullptr) {
      return absl::AlreadyExistsError(
          "An RDMA transport is already registered");
    }
    factory_ = std::make_unique<TransportFactory>(std::move(factory));
    return absl::OkStatus();
  }

  const TransportFactory* Find() const ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return factory_.get();
  }

 private:
  mutable absl::Mutex mu_;
  std::unique_ptr<TransportFactory> factory_ ABSL_GUARDED_BY(mu_);
};

}  // namespace

absl::Status RegisterRdmaTransport(TransportFactory factory) {
  return RdmaTransportSlot::Get().Set(std::move(factory));
}

namespace internal {

const TransportFactory* GetRegisteredRdmaTransport() {
  return RdmaTransportSlot::Get().Find();
}

bool RegisterRdmaTransportStatically(TransportFactory factory) {
  CHECK_OK(RdmaTransportSlot::Get().Set(std::move(factory)));
  return true;
}

}  // namespace internal
}  // namespace peregrine
