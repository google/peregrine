#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel.h"
#include "src/internal/control/control.h"
#include "src/internal/metrics/engine_metrics.h"
#include "src/internal/rdma/rdma_acceptor.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/util/macro.h"
#include "src/util/thread.h"

namespace peregrine::internal {

// Helper class for engine, which can focus on request scheduling.
// This class is thread-safe.
class EngineHelper final {
  using Channels = std::vector<std::unique_ptr<Channel>>;

 public:
  // Creates an engine helper and fills in the `self` host info.
  static std::unique_ptr<EngineHelper> Create(const Config& config,
                                              HostInfo& self, Control& control);

  // Disallows copy and move.
  DISALLOW_COPY(EngineHelper);
  DISALLOW_MOVE(EngineHelper);

  // Destructor.
  ~EngineHelper();

  // Returns a mutable reference to engine metrics.
  EngineMetrics& Metrics() { return metrics_; }

  // Creates a number of channels connected to the peer.
  Channels Connect(const Endpoint& peer_control);

  // Returns the accepted channels.
  Channels GetAcceptedChannels() ABSL_LOCKS_EXCLUDED(channels_mu_);

  // Registers a contiguous memory buffer for rdma.
  absl::Status RegisterMemory(void* addr, size_t length);

  // Unregisters a previously registered rdma memory buffer.
  absl::Status UnregisterMemory(const void* addr);

 private:
  // Constructor.
  EngineHelper(const Config& config, const HostInfo& self, Control& control,
               std::unique_ptr<TcpAcceptor> tcp_acceptor,
               std::unique_ptr<RdmaAcceptor> rdma_acceptor);

  // Returns true if the following invariants hold.
  bool invariant() const {
    return config_.IsValid() && self_.IsValid() && tcp_acceptor_ != nullptr;
  }

 private:
  // Accepts the incoming tcp `socket`.
  void accept(std::unique_ptr<TcpSocket> socket)
      ABSL_LOCKS_EXCLUDED(channels_mu_);

  // Creates `n` TCP channels that connect to the peer.
  Channels connectTcp(const Endpoint& peer_control, int n);

  // Creates `n` RDMA channels that connect to the peer.
  Channels connectRdma(const Endpoint& peer_control, int n);

  // Returns an error if the config requires RDMA but its acceptor is null.
  absl::Status checkRdmaAcceptor() const;

 private:
  const Config& config_;
  const HostInfo& self_;
  Control& control_;

  EngineMetrics metrics_;

  absl::Mutex channels_mu_;
  Channels accepted_channels_ ABSL_GUARDED_BY(channels_mu_);

  absl_nonnull std::unique_ptr<TcpAcceptor> tcp_acceptor_;
  absl_nullable std::unique_ptr<RdmaAcceptor> rdma_acceptor_;

  util::Jthread tcp_acceptor_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
