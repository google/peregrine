#include "peregrine/src/internal/engine2/channel_manager.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/connection_planner.h"
#include "peregrine/src/internal/engine2/tcp_manager.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"

namespace peregrine::internal::engine2 {

std::unique_ptr<ChannelManager> ChannelManager::Create(const Config& config,
                                                       HostInfo& self,
                                                       EngineMetrics& metrics) {
  if (ABSL_PREDICT_FALSE(!config.IsValid())) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  std::unique_ptr<TcpManager> tcp_manager =
      TcpManager::Create(config, self, metrics);
  if (ABSL_PREDICT_FALSE(tcp_manager == nullptr)) {
    LOG(ERROR) << "failed to create tcp manager for " << self;
    return nullptr;
  }
  return absl::WrapUnique(new ChannelManager(std::move(tcp_manager)));
}

ChannelManager::ChannelManager(std::unique_ptr<TcpManager> tcp_manager)
    : tcp_manager_(std::move(tcp_manager)) {}

void ChannelManager::Start(OnChannelReadyFn on_channel_ready,
                           OnConnectFailedFn on_connect_failed) {
  tcp_manager_->Start(std::move(on_channel_ready),
                      std::move(on_connect_failed));
}

void ChannelManager::Connect(const ConnectionSpec& spec) {
  tcp_manager_->Connect(spec);
}

}  // namespace peregrine::internal::engine2
