#include "peregrine/src/internal/engine2/worker.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::engine2 {
namespace {

// Non-blocking stream `Channel` backed by a real kernel `socketpair` fd.
class SocketPairChannel final : public Channel {
 public:
  explicit SocketPairChannel(int fd) : fd_(fd) {}
  ~SocketPairChannel() override {
    if (fd_ >= 0) ::close(fd_);
  }

  ChannelType Type() const override { return ChannelType::kTCP; }
  bool IsBlocking() const override { return false; }
  fd_t GetFd() const override { return fd_t(fd_); }

  ssize_t Write(IoVecCursor& iovecs) override {
    const absl::Span<const IoVec> span = iovecs.RemainingSpan();
    const ssize_t n = ::writev(fd_, span.data(), static_cast<int>(span.size()));
    if (n > 0) {
      iovecs.Advance(static_cast<size_t>(n));
      return n;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      return 0;
    }
    return -1;
  }

  ssize_t Read(IoVecCursor& iovecs) override {
    const absl::Span<const IoVec> span = iovecs.RemainingSpan();
    const ssize_t n = ::readv(fd_, span.data(), static_cast<int>(span.size()));
    if (n > 0) {
      iovecs.Advance(static_cast<size_t>(n));
      return n;
    }
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }

  void Shutdown() override {
    if (fd_ >= 0) ::shutdown(fd_, SHUT_WR);
  }
  std::string ToString() const override { return "socketpair"; }

 private:
  int fd_;
};

TEST(WorkerTest, TransfersMultiChunkPostOverSocketPair) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(
      ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds),
      0);

  HostInfo host_a;
  host_a.control_plane_listener = Endpoint::Create("127.0.0.1:10001");
  HostInfo host_b;
  host_b.control_plane_listener = Endpoint::Create("127.0.0.1:10002");

  EngineMetrics metrics_a;
  EngineMetrics metrics_b;
  TraceRecorder tracer_a(TraceConfig{.sample_every_n = 0});
  TraceRecorder tracer_b(TraceConfig{.sample_every_n = 0});
  RequestTracker outgoing_a(metrics_a, tracer_a);
  RequestTracker incoming_a(metrics_a, tracer_a);
  RequestTracker outgoing_b(metrics_b, tracer_b);
  RequestTracker incoming_b(metrics_b, tracer_b);
  PeerWorkQueue queue_a;
  PeerWorkQueue queue_b;

  Worker worker_a(/*id=*/0, /*rail_id=*/0, host_a, outgoing_a, incoming_a);
  Worker worker_b(/*id=*/1, /*rail_id=*/0, host_b, outgoing_b, incoming_b);

  worker_a.AddChannel(host_b.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[0]), queue_a);
  worker_b.AddChannel(host_a.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[1]), queue_b);

  const size_t len = kChunkSize * 2 + 777;
  std::vector<Byte> src(len);
  for (size_t i = 0; i < len; ++i) src[i] = static_cast<Byte>((i * 17) & 0xff);
  std::vector<Byte> dst(len, 0);

  const Handle handle(123);
  const uint32_t nchunks = static_cast<uint32_t>((len - 1) / kChunkSize + 1);
  std::vector<RequestInfo> req_infos = {RequestInfo{
      .reqid = ReqId(1),
      .nchunks = nchunks,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst.data())),
      .total_len = len,
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
  }};

  absl::Notification done;
  Status final_status = Status::kInProgress;
  ASSERT_TRUE(outgoing_a.Add(
      handle, /*start_mono_us=*/0,
      [&](Status s) {
        final_status = s;
        done.Notify();
      },
      TraceContext{}));
  ASSERT_TRUE(outgoing_a.Add(handle, req_infos, /*start_us=*/0));

  auto qp = std::make_shared<QueuedPost>();
  qp->post.handle = handle;
  qp->post.requests.push_back(Request{
      .op = Op::kWrite,
      .laddr = src.data(),
      .raddr = dst.data(),
      .len = len,
  });
  qp->req_infos = std::move(req_infos);
  queue_a.Enqueue(std::move(qp));
  worker_a.Wake();

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(final_status, Status::kSuccess);
  EXPECT_EQ(dst, src);
}

}  // namespace
}  // namespace peregrine::internal::engine2
