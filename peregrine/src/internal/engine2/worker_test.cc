#include "peregrine/src/internal/engine2/worker.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"

namespace peregrine::internal::engine2 {
namespace {

// Channel that carries no data; no test here transfers bytes.
class FakeChannel final : public Channel {
 public:
  ChannelType Type() const override { return ChannelType::kMemStream; }
  bool IsBlocking() const override { return false; }
  ssize_t Write(IoVecCursor&) override { return 0; }
  ssize_t Read(IoVecCursor&) override { return 0; }
  void Shutdown() override {}
  std::string ToString() const override { return "fake"; }
};

TEST(WorkerTest, AcceptsChannelsAndWakeUps) {
  PeerWorkQueue work_queue;
  Worker worker(/*rail_id=*/0);
  worker.AddChannel(std::make_unique<FakeChannel>(), work_queue);
  worker.Wake();
}

}  // namespace
}  // namespace peregrine::internal::engine2
