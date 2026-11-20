#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_

namespace peregrine::internal::engine2::assumptions {

// Assumptions about peer host info lookup.
// ---------------------------------------------------------------------------
//
// To connect to a peer, the engine needs the peer's `HostInfo`, obtained
// through `GetPeerHostInfoFn` (peer_discovery.h). In production this calls
// `Control::GetPeerHostInfo`, which is a synchronous RPC with a local cache.
//
// `TransferManager` invokes `GetPeerHostInfoFn` on a dedicated setup thread so
// app threads calling `Engine::Enqueue()` never block on network lookups.
// Lookup implementations must be bounded in time (e.g. RPC deadline) so one
// unreachable peer does not stall setup for other peers indefinitely.
inline constexpr bool kPeerHostInfoLookupRunsOnSetupThread = true;

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

// Assumptions about setup failures.
// ---------------------------------------------------------------------------
//
// If host info lookup fails, returns a mismatched peer, or every connect for a
// peer fails with no ready channels, the engine fails all posts queued for
// that peer (via `RequestTracker::Fail`) without retrying, and resets the peer
// to idle so the next post triggers setup again.
inline constexpr bool kSetupFailuresAbortQueuedPosts = true;

// Assumptions about simultaneous connects.
// ---------------------------------------------------------------------------
//
// Channels are bidirectional: an accepted inbound channel marks the peer ready
// so the receiving side skips host info lookup and outbound connects. If both
// peers connect to each other at the same time, both sets of channels (up to
// 2 * num_conns_per_peer) are kept and used rather than deduplicated.
inline constexpr bool kDuplicateChannelsAllowedOnSimultaneousConnect = true;

}  // namespace peregrine::internal::engine2::assumptions

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_ASSUMPTIONS_H_
