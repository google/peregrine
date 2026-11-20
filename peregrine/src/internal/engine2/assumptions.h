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
// Startup populates the local `HostInfo` in two steps:
//  1. `Engine::Create()` fills in `data_plane_listeners`.
//  2. The control plane starts and fills in `control_plane_listener` (its
//     port).
//
// After the control plane starts, `HostInfo` is immutable for the rest of the
// engine's lifetime. Engine components therefore avoid reading
// `control_plane_listener` during `Create()`/`Start()` and only read it during
// channel handshakes (which happen after the control plane is up), so no lock
// is needed to read it from background threads.
inline constexpr bool kLocalIdentityIsReadOnlyAfterControlPlaneStarts = true;

// Assumptions about channel manager callbacks and threading.
// ---------------------------------------------------------------------------
//
// `ChannelManager` reports results through two callbacks:
//  - `on_channel_ready`: runs on an internal I/O thread once a channel is
//    connected and its handshake completes. Transfers ownership of the channel
//    to the caller.
//  - `on_connect_failed`: runs on an internal I/O thread when a connect or
//    handshake fails or times out. Never runs inline inside `Connect()`.
//
// Consequently, callers must ensure both callbacks:
//  - Are thread-safe (multiple transport I/O threads may invoke them
//    concurrently once more than one transport is active).
//  - Do not block, since blocking stalls I/O for all other peers.
inline constexpr bool kChannelManagerCallbacksMayRunOnAnyThread = true;

// Assumptions about failures.
// ---------------------------------------------------------------------------
//
// For now, if getting a peer's host info or opening a channel to it fails, the
// engine aborts all posts queued for the peer and reports them failed to the
// app, without retrying. The next post to the peer gets its host info again.
inline constexpr bool kSetupFailuresAbortQueuedPosts = true;

}  // namespace peregrine::internal::engine2::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_
