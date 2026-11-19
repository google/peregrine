#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/channel_manager.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/trace/trace_context.h"
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
  // A batch of requests to one peer, identified by `handle`.
  struct Post {
    Handle handle;
    std::vector<Request> requests;
    Metadata metadata = {};
    TraceContext trace_ctx;
  };

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

  // Implementation details:
  // - Stops the channel manager, then the workers, then frees per-peer state.
  ~TransferManager() = default;

  // Queues `post` for transfer to `peer`. Returns without waiting.
  //
  // Implementation details:
  // - Adds `post` to `peer`'s queue: data chunks for writes, or one request
  //   frame for two-sided reads.
  // - Looks up `peer` and opens connections if needed.
  // - Wakes the workers serving `peer`.
  void Submit(const PeerId& peer, Post post);

  // Cancels all pending work for post `handle` to `peer` and releases its
  // state. Once cancellation completes, no worker accesses `handle`'s buffers.
  //
  // Implementation details:
  // - Drops `handle`'s queued work and lets any chunk already being written
  //   finish, so the stream stays framed.
  // - Reports to the outgoing tracker once every worker serving `peer` is done
  //   with `handle`.
  void Cancel(const PeerId& peer, Handle handle);

 private:
  explicit TransferManager(std::unique_ptr<ChannelManager> channel_manager);

  // Handles a channel accepted from a remote peer on `local_rail_id`.
  //
  // Implementation details:
  // - Hands the channel to a worker on `local_rail_id`, which learns the
  //   peer from the first frame.
  void onInboundReady(std::unique_ptr<Channel> ch, uint32_t local_rail_id);

  // Handles the outcome of a connect from `local_rail_id` to `peer`'s
  // `remote_rail_id`.
  //
  // Implementation details:
  // - On success, records the channel in `peer`'s state and hands it to a
  //   worker on `local_rail_id`.
  // - On failure, records it in `peer`'s state, which decides whether to
  //   retry, and fails `peer`'s posts if no channel to `peer` remains.
  void onOutboundDone(const PeerId& peer, uint32_t local_rail_id,
                      uint32_t remote_rail_id,
                      absl::StatusOr<std::unique_ptr<Channel>> ch);

 private:
  // Declared last so it is destroyed first, before its callbacks' targets.
  std::unique_ptr<ChannelManager> channel_manager_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_TRANSFER_MANAGER_H_
