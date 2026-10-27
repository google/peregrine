#include "peregrine/src/internal/transport_registry.h"

#include <cstdint>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine::internal {
namespace {

// `node_hash_map` keeps element addresses stable across insertions, so
// `FindRegisteredTransport()` can hand out pointers that outlive the lock.
class Registry final {
 public:
  static Registry& Get() {
    static absl::NoDestructor<Registry> registry;
    return *registry;
  }

  absl::Status Register(TransportType t, TransportRegistration registration)
      ABSL_LOCKS_EXCLUDED(mu_) {
    if (registration.create == nullptr ||
        registration.is_supported == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Incomplete registration for transport type ",
                       static_cast<uint32_t>(t)));
    }
    absl::MutexLock lock(mu_);
    if (!registrations_.try_emplace(t, std::move(registration)).second) {
      return absl::AlreadyExistsError(absl::StrCat("Transport type ",
                                                   static_cast<uint32_t>(t),
                                                   " is already registered"));
    }
    return absl::OkStatus();
  }

  const TransportRegistration* Find(TransportType t) const
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    auto it = registrations_.find(t);
    return it == registrations_.end() ? nullptr : &it->second;
  }

 private:
  mutable absl::Mutex mu_;
  absl::node_hash_map<TransportType, TransportRegistration> registrations_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace

absl::Status RegisterTransport(TransportType t,
                               TransportRegistration registration) {
  return Registry::Get().Register(t, std::move(registration));
}

const TransportRegistration* FindRegisteredTransport(TransportType t) {
  return Registry::Get().Find(t);
}

bool RegisterTransportStatically(TransportType t,
                                 TransportRegistration (*make_registration)()) {
  CHECK_OK(RegisterTransport(t, make_registration()));
  return true;
}

}  // namespace peregrine::internal
