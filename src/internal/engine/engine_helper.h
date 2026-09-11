#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_

#include <cstddef>
#include <memory>
#include <thread>  // NOLINT
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_metrics.h"
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

namespace peregrine::internal {

// Helper class for engine, which can focus on request scheduling.
// This class is thread-safe.
class EngineHelper final {
  using Channels = std::vector<std::unique_ptr<Channel>>;

 public:
  // Creates an engine helper and fills in the `self` host info.
  static std::unique_ptr<EngineHelper> Create(const Config& config,
                                              HostInfo& self, Control& control);

  // Disallows copy.
  DISALLOW_COPY(EngineHelper);

  // Allows move.
  ALLOW_MOVE(EngineHelper);

  // Destructor.
  ~EngineHelper();

  // Creates a number of channels connected to the `peer`.
  void Connect(const Endpoint& peer);

  // Returns the channels connected to the `peer`.
  Channels GetChannels(const Endpoint& peer);

  // Registers a contiguous memory buffer for rdma.
  absl::Status RegisterMemory(void* addr, size_t length);

  // Unregisters a previously registered rdma memory buffer.
  absl::Status UnregisterMemory(const void* addr);

  // Takes a snapshot of engine metrics.
  void GetMetrics(TransportMetrics& m) const { metrics_.Snapshot(m); }

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
  // Adds a `channel` for the `peer` and returns the number of channels.
  int addChannel(const Endpoint& peer, std::unique_ptr<Channel> channel);

  // Accepts the incoming tcp `socket`.
  void accept(std::unique_ptr<TcpSocket> socket);

  // Connects to a TCP `peer`.
  void connectTcp(const Endpoint& peer);

  // Connects to an RDMA `peer`.
  void connectRdma(const Endpoint& peer);

 private:
  const Config& config_;
  const HostInfo& self_;
  Control& control_;

  EngineHelperMetrics metrics_;

  std::unique_ptr<TcpAcceptor> tcp_acceptor_;    // nonnull
  std::unique_ptr<RdmaAcceptor> rdma_acceptor_;  // may be null

  absl::Mutex channels_mu_;
  absl::flat_hash_map<Endpoint, Channels> channels_;

  std::jthread tcp_acceptor_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_ENGINE_HELPER_H_
