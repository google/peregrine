#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/assumptions.h"

namespace peregrine::internal::engine2 {

// Synchronous peer discovery hook: resolves `peer` to its `HostInfo`.
// Invoked on `TransferManager`'s setup thread; see assumptions.h.
static_assert(assumptions::kPeerHostInfoLookupRunsOnSetupThread);
using GetPeerHostInfoFn =
    absl::AnyInvocable<absl::StatusOr<HostInfo>(const PeerId& peer)>;

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_
