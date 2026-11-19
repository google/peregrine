#include "peregrine/src/internal/engine2/transfer_manager.h"

#include <memory>

#include "absl/memory/memory.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"

namespace peregrine::internal::engine2 {

std::unique_ptr<TransferManager> TransferManager::Create(
    const Config& config, const HostInfo& self,
    GetPeerHostInfoFn get_peer_host_info_fn) {
  return absl::WrapUnique(new TransferManager());
}

void TransferManager::Submit(const PeerId& peer, Post post) {}

void TransferManager::Cancel(const PeerId& peer, Handle handle) {}

}  // namespace peregrine::internal::engine2
