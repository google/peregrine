#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_REGISTRY_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_REGISTRY_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"

namespace peregrine::internal {

// A process-wide registry of transport implementations that are not built
// into `peregrine/src/api:transport_util`.
//
// This lets a transport whose dependencies are not compatible with every
// environment Peregrine supports (e.g., an internal-only transport) live in a
// separate, opt-in library. That library registers itself, typically at
// static-initialization time via `PEREGRINE_REGISTER_TRANSPORT` (see below),
// and `CreateTransport()` / `IsTransportSupported()` dispatch to it, without
// `transport_util` depending on it at link time.
//
// Registrations are never removed. All functions are thread-safe.
struct TransportRegistration {
  // Creates a transport with the given `config` and control `endpoint`.
  // Returns nullptr on failure.
  absl::AnyInvocable<std::unique_ptr<Transport>(
      const Config& config, const Endpoint& endpoint, SecurityCredentials creds)
                         const>
      create;

  // Returns true if the transport is supported on the current host.
  absl::AnyInvocable<bool() const> is_supported;
};

// Registers `registration` for the transport type `t`.
//
// Returns InvalidArgumentError if `registration` has a null `create` or
// `is_supported`, and AlreadyExistsError if `t` is already registered.
absl::Status RegisterTransport(TransportType t,
                               TransportRegistration registration);

// Returns the registration for the transport type `t`, or nullptr if `t` is not
// registered. The returned pointer remains valid for the process lifetime.
const TransportRegistration* FindRegisteredTransport(TransportType t);

// Implementation detail of `PEREGRINE_REGISTER_TRANSPORT`; do not call
// directly. Registers `make_registration()` for `t` and crashes on failure
// (e.g., a duplicate registration), since that is a programming error.
// Always returns true.
bool RegisterTransportStatically(TransportType t,
                                 TransportRegistration (*make_registration)());

}  // namespace peregrine::internal

// Registers a transport at static-initialization time, so that any binary that
// links the registering library can use it via `CreateTransport()` without
// calling anything from `main()`.
//
// Usage, at namespace scope in a .cc file of a `cc_library` that sets
// `alwayslink = True` (otherwise the linker may drop the unreferenced object
// file and the registration with it):
//
//   TransportRegistration MakeFooRegistration() { return {...}; }
//   PEREGRINE_REGISTER_TRANSPORT(foo, TransportType::kFoo,
//                                MakeFooRegistration);
//
// `name` must be unique within the enclosing namespace.
#define PEREGRINE_REGISTER_TRANSPORT(name, transport_type, make_registration) \
  [[maybe_unused]] static const bool                                          \
      peregrine_transport_registration_##name##_ =                            \
          ::peregrine::internal::RegisterTransportStatically(                 \
              transport_type, make_registration)

#endif  // PEREGRINE_SRC_INTERNAL_TRANSPORT_REGISTRY_H_
