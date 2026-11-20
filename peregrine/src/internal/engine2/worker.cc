#include "peregrine/src/internal/engine2/worker.h"

#include <cstdint>
#include <memory>

#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"

namespace peregrine::internal::engine2 {

Worker::Worker(uint32_t rail_id) : rail_id_(rail_id) {}

void Worker::AddChannel(std::unique_ptr<Channel> ch,
                        PeerWorkQueue& work_queue) {}

void Worker::Wake() {}

}  // namespace peregrine::internal::engine2
