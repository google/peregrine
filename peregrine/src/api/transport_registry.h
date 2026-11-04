#ifndef PEREGRINE_SRC_API_TRANSPORT_REGISTRY_H_
#define PEREGRINE_SRC_API_TRANSPORT_REGISTRY_H_

#include <memory>
#include <string_view>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine {

// DEPRECATED / TEMPORARY: lets a separately linked transport stand in for the
// built-in RDMA transport until it is integrated into Peregrine; this hook will
// then be removed. Do not use in new code.
//
// If an RDMA transport is registered, `CreateTransport()` (see
// transport_util.h) with `TransportType::kRdma` uses it; otherwise the built-in
// RDMA transport is used. No other transport type is affected.

// Creates a transport from the same inputs as `CreateTransport()`:
//  - `endpoint` is the transport's control-plane identity ("ip:port"; port 0
//    means kernel-assigned), as passed by the application. It may be invalid;
//    the factory must validate it.
//  - `options.num_parallel_connections` is already clamped to [1, 100]; the
//    callbacks in `options` must be honored.
// The transport owns its data plane, including discovering and selecting NICs.
// Returns nullptr on failure.
using TransportFactory = absl::AnyInvocable<std::unique_ptr<Transport>(
    std::string_view endpoint, Options options) const>;

// Registers `factory` as the RDMA transport.
//
// Returns InvalidArgumentError if `factory` is null, and AlreadyExistsError if
// an RDMA transport is already registered. Thread-safe.
ABSL_DEPRECATED(
    "Temporary RDMA override; will be removed once the replacement "
    "transport is integrated into Peregrine.")
absl::Status RegisterRdmaTransport(TransportFactory factory);

namespace internal {

// Returns the registered RDMA transport factory, or nullptr if none. The
// returned pointer remains valid for the process lifetime. Thread-safe.
const TransportFactory* GetRegisteredRdmaTransport();

// Implementation detail of `PEREGRINE_REGISTER_RDMA_TRANSPORT`; do not call
// directly. Registers `factory` as the RDMA transport and crashes on failure
// (e.g., a duplicate registration), since that is a programming error. Always
// returns true.
bool RegisterRdmaTransportStatically(TransportFactory factory);

}  // namespace internal
}  // namespace peregrine

// DEPRECATED / TEMPORARY (see above). Registers `factory` as the RDMA transport
// at static-initialization time, so that any binary that links the registering
// library uses it for `TransportType::kRdma` without calling anything from
// `main()`.
//
// Use at most once per binary, at namespace scope in a .cc file of a
// `cc_library` that sets `alwayslink = True` (otherwise the linker may drop the
// unreferenced object file and the registration with it):
//
//   std::unique_ptr<peregrine::Transport> CreateFooTransport(
//       std::string_view endpoint, peregrine::Options options) { ... }
//   PEREGRINE_REGISTER_RDMA_TRANSPORT(CreateFooTransport);
#define PEREGRINE_REGISTER_RDMA_TRANSPORT(factory)                          \
  [[maybe_unused]] static const bool peregrine_rdma_transport_registered_ = \
      ::peregrine::internal::RegisterRdmaTransportStatically(factory)

#endif  // PEREGRINE_SRC_API_TRANSPORT_REGISTRY_H_
