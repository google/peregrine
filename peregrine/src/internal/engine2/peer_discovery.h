#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/assumptions.h"

namespace peregrine::internal::engine2 {

// Peer discovery: resolves `peer` to its `HostInfo` and reports it via
// `on_done`. Supplied by the engine's owner; see assumptions.h for the
// contract.
static_assert(assumptions::kPeerHostInfoLookupIsAsynchronous);
using GetPeerHostInfoFn = absl::AnyInvocable<void(
    const PeerId& peer,
    absl::AnyInvocable<void(absl::StatusOr<HostInfo>)> on_done)>;

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_DISCOVERY_H_
