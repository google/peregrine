#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_

namespace peregrine::internal::engine2::assumptions {

// Assumptions about peer host info lookup.
// ---------------------------------------------------------------------------
//
// To connect to a peer, the engine needs the peer's `HostInfo`, which it
// obtains through a peer discovery hook (`GetPeerHostInfoFn` in
// peer_discovery.h) supplied by its owner. In production the hook asks the
// peer over the control plane; in tests it may read from an in-process table.
// The engine calls the hook with the peer's control-plane endpoint and a
// completion callback `on_done`, at most once at a time per peer, and caches
// the result.
//
// A lookup may take a network round trip, while the engine must keep serving
// requests to other peers. So peer host info lookup is asynchronous, and hook
// implementations must:
//  - Return without blocking; slow work such as an RPC runs asynchronously.
//  - Invoke `on_done` exactly once with the `HostInfo` or an error, including
//    on timeout or shutdown, so requests waiting on the peer always complete.
//    Lookups should be bounded in time.
//  - Expect that `on_done` may run on any thread, including inside the hook
//    call; the engine never holds its locks while calling the hook. `on_done`
//    must not block.
inline constexpr bool kPeerHostInfoLookupIsAsynchronous = true;

// Assumptions about the local host info.
// ---------------------------------------------------------------------------
//
// The engine's owner owns the local `HostInfo` and keeps it alive for the
// engine's lifetime. `Engine::Create()` fills in its data-plane part, and the
// control plane fills in the control-plane port only after the engine is
// created. Engine components therefore read their own identity
// (`control_plane_listener`) only when opening outbound channels, which starts
// with the first `Engine::Enqueue()`, after the control plane has started.
inline constexpr bool kLocalIdentityIsReadOnlyAfterControlPlaneStarts = true;

// Assumptions about channel manager callbacks.
// ---------------------------------------------------------------------------
//
// `ChannelManager` reports accepted channels to `on_inbound_ready` and the
// outcome of each `Connect()` to `on_outbound_done`. Both callbacks:
//  - May run on any thread, including inside `Connect()` itself. So callers
//    must not hold a lock that the callbacks take while calling `Connect()`.
//  - Must not block, since they run on the channel manager's threads.
//  - Take ownership of the channel; the channel manager keeps no reference.
inline constexpr bool kChannelManagerCallbacksMayRunOnAnyThread = true;

}  // namespace peregrine::internal::engine2::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_
