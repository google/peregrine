#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_state.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Transfers posts to peers in parallel.
//
// `TransferManager` takes each post from `Engine`, divides it into small
// pieces, and transfers them to the peer in parallel. It also manages each
// transfer through cancellations and connection disruptions.
//
// Thread-safety: This class is thread-safe.
class TransferManager final {
 public:
  // Creates a transfer manager for the local host described by `self`, which
  // must outlive the transfer manager. `get_peer_host_info_fn` looks up remote
  // peers. Takes ownership of `channel_manager` and starts it. Returns nullptr
  // on failure.
  //
  // Implementation details:
  // - Creates workers for each local rail, pinned to CPUs on the rail's NUMA
  //   node.
  static std::unique_ptr<TransferManager> Create(
      const Config& config, const HostInfo& self,
      GetPeerHostInfoFn get_peer_host_info_fn,
      std::unique_ptr<ChannelManager> channel_manager);

  DISALLOW_COPY(TransferManager);
  DISALLOW_MOVE(TransferManager);

  // Stops host info callbacks from reaching this object, then stops the
  // channel manager and frees per-peer state.
  ~TransferManager();

  // Queues `post` for transfer to `peer`. Returns without waiting. Gets
  // `peer`'s host info if it is new.
  //
  // Implementation details:
  // - Wakes the workers serving `peer`.
  void Submit(const PeerId& peer, Post post);

  // Cancels all pending work for post `handle` to `peer` and releases its
  // state. Once cancellation completes, no worker accesses `handle`'s buffers.
  //
  // Implementation details:
  // - Lets any chunk already being written finish, so the stream stays framed.
  // - Reports to the outgoing tracker once every worker serving `peer` is done
  //   with `handle`.
  void Cancel(const PeerId& peer, Handle handle);

 private:
  TransferManager(const Config& config, const HostInfo& self,
                  GetPeerHostInfoFn get_peer_host_info_fn,
                  std::unique_ptr<ChannelManager> channel_manager);

  // Returns the state of `peer`, creating it if needed.
  PeerState& getOrCreatePeer(const PeerId& peer) ABSL_LOCKS_EXCLUDED(mu_);

  // Returns the state of `peer`, or nullptr if there is none.
  PeerState* findPeer(const PeerId& peer) ABSL_LOCKS_EXCLUDED(mu_);

  // Handles `peer`'s host info, and opens the connections planned for it. On
  // failure, or if the host info is not `peer`'s, aborts `peer`'s posts.
  void onPeerHostInfo(const PeerId& peer, absl::StatusOr<HostInfo> host_info);

  // Handles a channel accepted from a remote peer on `local_rail_id`.
  //
  // Implementation details:
  // - Hands the channel to a worker on `local_rail_id`, which learns the
  //   peer from the first frame.
  void onInboundReady(std::unique_ptr<Channel> ch, uint32_t local_rail_id);

  // Handles the outcome of a connect from `local_rail_id` to `peer`'s
  // `remote_rail_id`. On failure, aborts `peer`'s posts.
  //
  // Implementation details:
  // - On success, hands the channel to a worker on `local_rail_id`.
  void onOutboundDone(const PeerId& peer, uint32_t local_rail_id,
                      uint32_t remote_rail_id,
                      absl::StatusOr<std::unique_ptr<Channel>> ch);

  // Aborts all posts queued for `peer` after a setup failure.
  void abortPosts(const PeerId& peer);

 private:
  // Lets host info callbacks reach the transfer manager only while it is
  // alive. A lookup may complete after the transfer manager is destroyed, so
  // its callback holds this guard instead of a raw `this`.
  struct LifetimeGuard {
    absl::Mutex mu;
    TransferManager* transfer_manager ABSL_GUARDED_BY(mu);
  };

 private:
  const Config config_;
  const HostInfo& self_;
  GetPeerHostInfoFn get_peer_host_info_fn_;
  const std::shared_ptr<LifetimeGuard> guard_;

  // Per-peer state. Entries are never removed while the transfer manager is
  // alive, so references to them stay valid without holding `mu_`.
  absl::Mutex mu_;
  absl::flat_hash_map<PeerId, std::unique_ptr<PeerState>> peers_
      ABSL_GUARDED_BY(mu_);

  // Declared last so it is destroyed first, before its callbacks' targets.
  std::unique_ptr<ChannelManager> channel_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
