#include "peregrine/src/internal/engine2/worker.h"

#include <cstdint>
#include <memory>

#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/peer_state.h"

namespace peregrine::internal::engine2 {

Worker::Worker(uint32_t rail_id) : rail_id_(rail_id) {}

void Worker::AddOutboundChannel(std::unique_ptr<Channel> ch,
                                PeerState& peer_state) {}

void Worker::AddInboundChannel(std::unique_ptr<Channel> ch) {}

void Worker::Wake() {}

}  // namespace peregrine::internal::engine2
