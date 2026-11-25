#include "peregrine/src/internal/engine2/worker.h"

#include <sched.h>
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
#include "peregrine/src/internal/lib/numa.h"
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

  const size_t len = kChunkSize * 2 + 777;
  std::vector<Byte> src(len);
  for (size_t i = 0; i < len; ++i) src[i] = static_cast<Byte>((i * 17) & 0xff);
  std::vector<Byte> dst(len, 0);
  std::vector<Byte> src2(64 * 1024, 0xAB);
  std::vector<Byte> dst2(src2.size(), 0);

  EngineMetrics metrics_a;
  EngineMetrics metrics_b;
  TraceRecorder tracer_a(TraceConfig{.sample_every_n = 0});
  TraceRecorder tracer_b(TraceConfig{.sample_every_n = 0});
  RequestTracker outgoing_a(metrics_a, tracer_a);
  RequestTracker incoming_a(metrics_a, tracer_a);
  RequestTracker outgoing_b(metrics_b, tracer_b);
  RequestTracker incoming_b(
      metrics_b, tracer_b, /*recv_post_callback=*/nullptr,
      [&](const Metadata&, const Metadata& req_meta) -> absl::Span<Byte> {
        if (req_meta[0] == 1) return absl::MakeSpan(dst);
        if (req_meta[0] == 2) return absl::MakeSpan(dst2);
        return {};
      });
  PeerWorkQueue queue_a;
  PeerWorkQueue queue_b;

  Worker worker_a(/*id=*/0, /*rail_id=*/0, host_a, outgoing_a, incoming_a,
                  tracer_a);
  Worker worker_b(/*id=*/1, /*rail_id=*/0, host_b, outgoing_b, incoming_b,
                  tracer_b);

  worker_a.AddChannel(host_b.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[0]), queue_a);
  worker_b.AddChannel(host_a.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[1]), queue_b);

  const Handle handle(123);
  const uint32_t nchunks = static_cast<uint32_t>((len - 1) / kChunkSize + 1);
  Metadata meta1 = {};
  meta1[0] = 1;
  std::vector<RequestInfo> req_infos = {RequestInfo{
      .reqid = ReqId(1),
      .nchunks = nchunks,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst.data())),
      .total_len = len,
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
      .metadata = meta1,
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
  ASSERT_TRUE(outgoing_a.AddInitiatorReqs(handle, req_infos));

  auto qp = std::make_shared<QueuedPost>();
  qp->post.handle = handle;
  qp->post.requests.push_back(Request{
      .op = Op::kWrite,
      .laddr = src.data(),
      .raddr = dst.data(),
      .len = len,
      .metadata = meta1,
  });
  qp->req_infos = std::move(req_infos);
  queue_a.Enqueue(std::move(qp));
  worker_a.Wake(host_b.control_plane_listener);

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(final_status, Status::kSuccess);
  EXPECT_EQ(dst, src);

  // Second targeted wake after the worker has gone idle exercises waking an
  // already-registered channel by its peer ID.
  const Handle handle2(124);
  Metadata meta2 = {};
  meta2[0] = 2;
  std::vector<RequestInfo> req_infos2 = {RequestInfo{
      .reqid = ReqId(2),
      .nchunks = 1,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst2.data())),
      .total_len = src2.size(),
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
      .metadata = meta2,
  }};
  absl::Notification done2;
  Status final_status2 = Status::kInProgress;
  ASSERT_TRUE(outgoing_a.Add(
      handle2, /*start_mono_us=*/0,
      [&](Status s) {
        final_status2 = s;
        done2.Notify();
      },
      TraceContext{}));
  ASSERT_TRUE(outgoing_a.AddInitiatorReqs(handle2, req_infos2));

  auto qp2 = std::make_shared<QueuedPost>();
  qp2->post.handle = handle2;
  qp2->post.requests.push_back(Request{
      .op = Op::kWrite,
      .laddr = src2.data(),
      .raddr = dst2.data(),
      .len = src2.size(),
      .metadata = meta2,
  });
  qp2->req_infos = std::move(req_infos2);
  queue_a.Enqueue(std::move(qp2));
  worker_a.Wake(host_b.control_plane_listener);

  ASSERT_TRUE(done2.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(final_status2, Status::kSuccess);
  EXPECT_EQ(dst2, src2);
}

TEST(WorkerTest, PinsWorkerThreadToConfiguredCpu) {
  int target_cpu = -1;
  for (int node = 0; node < 64; ++node) {
    const std::vector<int> cpus = GetNumaCpus(node);
    if (!cpus.empty()) {
      target_cpu = cpus.front();
      break;
    }
  }
  ASSERT_GE(target_cpu, 0);

  int fds[2] = {-1, -1};
  ASSERT_EQ(
      ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds),
      0);

  HostInfo host_a;
  host_a.control_plane_listener = Endpoint::Create("127.0.0.1:10011");
  HostInfo host_b;
  host_b.control_plane_listener = Endpoint::Create("127.0.0.1:10012");

  std::vector<Byte> src(1024, 0x42);
  std::vector<Byte> dst(src.size(), 0);

  EngineMetrics metrics_a;
  EngineMetrics metrics_b;
  TraceRecorder tracer_a(TraceConfig{.sample_every_n = 0});
  TraceRecorder tracer_b(TraceConfig{.sample_every_n = 0});
  RequestTracker outgoing_a(metrics_a, tracer_a);
  RequestTracker incoming_a(metrics_a, tracer_a);
  RequestTracker outgoing_b(metrics_b, tracer_b);
  RequestTracker incoming_b(
      metrics_b, tracer_b, /*recv_post_callback=*/nullptr,
      [&](const Metadata&, const Metadata&) -> absl::Span<Byte> {
        return absl::MakeSpan(dst);
      });
  PeerWorkQueue queue_a;
  PeerWorkQueue queue_b;

  // Sender worker pins to the chosen CPU; receiver worker exercises graceful
  // fallback on an out-of-range CPU ID.
  Worker worker_a(/*id=*/0, /*rail_id=*/0, host_a, outgoing_a, incoming_a,
                  tracer_a, target_cpu);
  Worker worker_b(/*id=*/1, /*rail_id=*/0, host_b, outgoing_b, incoming_b,
                  tracer_b, /*cpu_id=*/65535);

  worker_a.AddChannel(host_b.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[0]), queue_a);
  worker_b.AddChannel(host_a.control_plane_listener,
                      std::make_unique<SocketPairChannel>(fds[1]), queue_b);

  const Handle handle(200);
  std::vector<RequestInfo> req_infos = {RequestInfo{
      .reqid = ReqId(1),
      .nchunks = 1,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst.data())),
      .total_len = src.size(),
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
  }};

  absl::Notification done;
  int running_cpu = -1;
  std::vector<int> worker_cpus;
  ASSERT_TRUE(outgoing_a.Add(
      handle, /*start_mono_us=*/0,
      [&](Status) {
        // Completion callback runs on the sender worker's epoll thread on ACK.
        running_cpu = ::sched_getcpu();
        cpu_set_t set;
        CPU_ZERO(&set);
        if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
          for (int c = 0; c < CPU_SETSIZE; ++c) {
            if (CPU_ISSET(c, &set)) worker_cpus.push_back(c);
          }
        }
        done.Notify();
      },
      TraceContext{}));
  ASSERT_TRUE(outgoing_a.AddInitiatorReqs(handle, req_infos));

  auto qp = std::make_shared<QueuedPost>();
  qp->post.handle = handle;
  qp->post.requests.push_back(Request{
      .op = Op::kWrite,
      .laddr = src.data(),
      .raddr = dst.data(),
      .len = src.size(),
  });
  qp->req_infos = std::move(req_infos);
  queue_a.Enqueue(std::move(qp));
  worker_a.Wake(host_b.control_plane_listener);

  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(dst, src);
  EXPECT_EQ(running_cpu, target_cpu);
  EXPECT_EQ(worker_cpus, std::vector<int>{target_cpu});
}

}  // namespace
}  // namespace peregrine::internal::engine2
