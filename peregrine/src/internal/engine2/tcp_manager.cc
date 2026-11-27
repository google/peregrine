#include "peregrine/src/internal/engine2/tcp_manager.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/constants.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel_tcp.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/connection_planner.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/clock.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {
namespace {

constexpr uint32_t kIncomingEvents = EPOLLIN;
constexpr uint32_t kConnectingEvents = EPOLLOUT | EPOLLRDHUP;
constexpr uint32_t kHandshakeEvents = EPOLLIN | EPOLLRDHUP;
constexpr absl::Duration kInitTimeout = absl::Seconds(5);

struct Hello {
  uint8_t ip[16];
  uint16_t port;
  uint8_t is_ipv6;
  uint8_t pad;
  int32_t rail_id;
};
static_assert(sizeof(Hello) == 24);

std::string EvtMsg(std::string_view what, const fd_t fd, const uint32_t flag,
                   const int err = 0) {
  return absl::StrFormat("%s fd=%d events=%#x errno=%d @ %s", what, fd.value(),
                         flag, err, AddrPortPair(fd));
}

bool WriteHello(const TcpSocket& socket, const PeerId& self,
                const int32_t rail_id) {
  Hello hello = {};
  const auto [raw, len] = self.GetIpAddr().RawAddrAndLen();
  DCHECK_LE(len, sizeof(hello.ip));
  std::memcpy(hello.ip, raw, len);
  hello.port = self.Port();
  hello.is_ipv6 = self.GetIpAddr().IsIPv6() ? 1 : 0;
  hello.rail_id = rail_id;
  IoVecCursor cursor({IoVec{&hello, sizeof(hello)}});
  return socket.Send(cursor) == static_cast<ssize_t>(sizeof(hello));
}

std::optional<std::pair<PeerId, int32_t>> ParseHello(const char* bytes) {
  Hello hello;
  std::memcpy(&hello, bytes, sizeof(hello));
  PeerId peer;
  if (hello.is_ipv6) {
    util::ipv6_t ip6;
    std::memcpy(&ip6, hello.ip, sizeof(ip6));
    peer = Endpoint(ip6, port_t(hello.port));
  } else {
    util::ipv4_t ip4;
    std::memcpy(&ip4, hello.ip, sizeof(ip4));
    peer = Endpoint(ip4, port_t(hello.port));
  }
  if (!peer.HasNonzeroIpPort() || hello.rail_id < kNoRail) return std::nullopt;
  return std::make_pair(peer, hello.rail_id);
}

std::unique_ptr<TcpSocket> CreateListener(Endpoint& endpoint, Poller& poller) {
  DCHECK(!endpoint.HasZeroIpAddr());
  auto socket = TcpSocket::Create(endpoint.GetIpAddr().AddressFamily(),
                                  /*blocking=*/false);
  if (ABSL_PREDICT_FALSE(socket == nullptr)) return nullptr;
  if (ABSL_PREDICT_FALSE(socket->Listen(endpoint) < 0)) return nullptr;
  const Endpoint e = SelfEndpoint(socket->fd());
  if (ABSL_PREDICT_FALSE(!e.HasNonzeroIpPort())) return nullptr;
  if (ABSL_PREDICT_FALSE(poller.Register(socket->fd(), kIncomingEvents) < 0)) {
    return nullptr;
  }
  endpoint = e;
  LOG(INFO) << "created, " << *socket;
  return socket;
}

}  // namespace

std::unique_ptr<TcpManager> TcpManager::Create(const Config& config,
                                               HostInfo& self,
                                               EngineMetrics& metrics) {
  if (ABSL_PREDICT_FALSE(!config.IsValid())) {
    LOG(ERROR) << "invalid config";
    return nullptr;
  }
  std::unique_ptr<Poller> poller = Poller::Create();
  if (ABSL_PREDICT_FALSE(poller == nullptr)) return nullptr;

  const int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (ABSL_PREDICT_FALSE(efd < 0)) {
    LOG(ERROR) << "eventfd failed: errno=" << errno;
    return nullptr;
  }
  const fd_t event_fd(efd);
  if (ABSL_PREDICT_FALSE(poller->Register(event_fd, EPOLLIN) < 0)) {
    ::close(efd);
    return nullptr;
  }

  const bool loopback = self.control_plane_listener.GetIpAddr().IsLoopback();
  auto candidates =
      NicInfo::GetTcpListenerCandidates(loopback, config.allowed_devices);
  if (ABSL_PREDICT_FALSE(candidates.empty())) {
    LOG(ERROR) << "failed to find data plane tcp listener candidates";
    ::close(efd);
    return nullptr;
  }

  HostInfo host = self;  // only update `self` on success
  absl::flat_hash_map<fd_t, Listener> listeners;
  for (auto& [_, ni] : candidates) {
    const std::optional<int32_t> rail_id = RailIdForInterface(host, ni.name);
    if (!rail_id.has_value()) continue;
    NicInfo nic(ni.name, ni.type, {});
    for (auto& e : ni.endpoints) {
      if (auto socket = CreateListener(e, *poller); socket != nullptr) {
        const fd_t fd = socket->fd();
        listeners.emplace(fd, Listener{std::move(socket), e, *rail_id});
        nic.endpoints.push_back(e);
      }
    }
    if (nic.IsValid()) host.data_plane_listeners.push_back(std::move(nic));
  }
  if (ABSL_PREDICT_FALSE(listeners.empty() || !host.IsDataPlaneValid())) {
    LOG(ERROR) << "failed to create data plane tcp listening sockets";
    listeners.clear();
    ::close(efd);
    return nullptr;
  }

  self = std::move(host);
  return absl::WrapUnique(new TcpManager(self, metrics, std::move(poller),
                                         event_fd, std::move(listeners)));
}

TcpManager::TcpManager(const HostInfo& self, EngineMetrics& metrics,
                       std::unique_ptr<Poller> poller, const fd_t event_fd,
                       absl::flat_hash_map<fd_t, Listener> listeners)
    : self_(self),
      metrics_(metrics),
      poller_(std::move(poller)),
      event_fd_(event_fd),
      listeners_(std::move(listeners)) {
  static_assert(sizeof(Hello) == kHelloSize);
}

TcpManager::~TcpManager() {
  Stop();
  if (poll_thread_.joinable()) poll_thread_.join();
  inits_.clear();
  listeners_.clear();
  ::close(event_fd_.value());
}

void TcpManager::Start(OnChannelReadyFn on_channel_ready,
                       OnConnectFailedFn on_connect_failed) {
  static_assert(assumptions::kChannelManagerCallbacksMayRunOnAnyThread);
  DCHECK(on_channel_ready != nullptr);
  DCHECK(on_connect_failed != nullptr);
  DCHECK(on_channel_ready_ == nullptr) << "Start() called twice";
  on_channel_ready_ = std::move(on_channel_ready);
  on_connect_failed_ = std::move(on_connect_failed);
  poll_thread_ = util::Jthread([this] { runLoop(); });
}

void TcpManager::Stop() {
  {
    absl::MutexLock lock(mu_);
    sealed_ = true;
    pending_.clear();
  }
  stop_.store(true, std::memory_order_release);
  wake();
}

void TcpManager::Connect(const ConnectionSpec& spec) {
  DCHECK(on_channel_ready_ != nullptr) << "Connect() called before Start()";
  bool should_wake = false;
  {
    absl::MutexLock lock(mu_);
    if (ABSL_PREDICT_FALSE(sealed_)) return;
    should_wake = pending_.empty();
    pending_.push_back(spec);
  }
  if (should_wake) wake();
}

void TcpManager::wake() {
  uint64_t one = 1;
  (void)::write(event_fd_.value(), &one, sizeof(one));
}

void TcpManager::drainPending() {
  std::vector<ConnectionSpec> batch;
  {
    absl::MutexLock lock(mu_);
    batch.swap(pending_);
  }
  for (const ConnectionSpec& spec : batch) {
    if (isStopped()) break;
    startConnect(spec);
  }
}

void TcpManager::startConnect(const ConnectionSpec& spec) {
  if (ABSL_PREDICT_FALSE(
          !spec.peer.HasNonzeroIpPort() || !spec.remote_ep.HasNonzeroIpPort() ||
          spec.local_rail_id < kNoRail || spec.remote_rail_id < kNoRail)) {
    reportConnectFailed(
        spec.peer, spec.local_rail_id, spec.remote_rail_id,
        absl::FailedPreconditionError("invalid connection spec"));
    return;
  }
  auto socket = TcpSocket::Create(spec.remote_ep.GetIpAddr().AddressFamily(),
                                  /*blocking=*/false);
  if (ABSL_PREDICT_FALSE(socket == nullptr ||
                         (!spec.local_bind.HasZeroIpAddr() &&
                          socket->Bind(spec.local_bind) < 0))) {
    reportConnectFailed(spec.peer, spec.local_rail_id, spec.remote_rail_id,
                        absl::UnavailableError("connect failed"));
    return;
  }

  ChannelInit init = {
      .state = ChannelInit::State::kConnecting,
      .socket = std::move(socket),
      .local_rail_id = spec.local_rail_id,
      .expected = PeerRail{spec.peer, spec.remote_rail_id},
      .expiration = util::MonotonicNow() + kInitTimeout,
  };
  switch (init.socket->Connect(spec.remote_ep)) {
    case kConnectSuccess:
      beginHandshake(std::move(init));
      return;
    case kConnectInProgress: {
      const fd_t fd = init.socket->fd();
      if (ABSL_PREDICT_FALSE(poller_->Register(fd, kConnectingEvents) < 0)) {
        finishInit(std::move(init),
                   absl::InternalError("failed to register connector"));
        return;
      }
      LOG(INFO) << "connecting " << *init.socket;
      inits_.emplace(fd, std::move(init));
      return;
    }
    default:
      finishInit(std::move(init), absl::UnavailableError("connect failed"));
      return;
  }
}

void TcpManager::beginHandshake(ChannelInit init) {
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);
  if (ABSL_PREDICT_FALSE(!WriteHello(*init.socket, self_.control_plane_listener,
                                     init.local_rail_id))) {
    finishInit(std::move(init), absl::UnavailableError("failed to send hello"));
    return;
  }
  init.state = ChannelInit::State::kHandshaking;
  init.expiration = util::MonotonicNow() + kInitTimeout;
  const fd_t fd = init.socket->fd();
  if (ABSL_PREDICT_FALSE(poller_->Register(fd, kHandshakeEvents) < 0)) {
    finishInit(std::move(init),
               absl::InternalError("failed to register channel"));
    return;
  }
  inits_.emplace(fd, std::move(init));
}

bool TcpManager::handleAccept(const fd_t fd, const uint32_t flag) {
  const auto it = listeners_.find(fd);
  if (it == listeners_.end()) return false;

  if (ABSL_PREDICT_FALSE(flag & (EPOLLERR | EPOLLHUP))) {
    LOG(WARNING) << EvtMsg("listening", fd, flag);
    poller_->Unregister(fd);
    listeners_.erase(it);
    return true;
  }
  if (!(flag & EPOLLIN)) return true;

  const TcpSocket& listener = *it->second.socket;
  const int32_t rail_id = it->second.local_rail_id;
  const int family = listener.family();
  while (!isStopped()) {
    const int ret = listener.Accept(/*gen_blocking=*/false);
    if (ABSL_PREDICT_FALSE(ret < 0)) {
      if (IsShutdown(ret)) {
        poller_->Unregister(fd);
        listeners_.erase(it);
      } else if (IsOutOfResource(ret)) {
        LOG_EVERY_N_SEC(ERROR, 3) << EvtMsg("out of resource", fd, flag);
        absl::SleepFor(absl::Milliseconds(50));
      } else if (!IsWouldBlock(ret)) {
        LOG_EVERY_N_SEC(ERROR, 1) << EvtMsg("accept failed", fd, flag);
      }
      break;
    }
    auto socket = TcpSocket::Create(fd_t(ret), family, /*blocking=*/false);
    if (ABSL_PREDICT_FALSE(socket == nullptr)) continue;
    LOG(INFO) << "accepted " << *socket;
    beginHandshake(ChannelInit{
        .socket = std::move(socket),
        .local_rail_id = rail_id,
    });
  }
  return true;
}

bool TcpManager::handleInit(const fd_t fd, const uint32_t flag) {
  const auto it = inits_.find(fd);
  if (it == inits_.end()) return false;

  ChannelInit& init = it->second;
  if (init.state == ChannelInit::State::kConnecting) {
    ChannelInit taken = std::move(init);
    inits_.erase(it);
    poller_->Unregister(fd);

    const int err = GetSocketError(fd);
    if (ABSL_PREDICT_FALSE(err != 0 ||
                           (flag & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) ||
                           !(flag & EPOLLOUT))) {
      LOG_EVERY_N_SEC(ERROR, 1) << EvtMsg("connecting", fd, flag, err);
      finishInit(std::move(taken), absl::UnavailableError("connect failed"));
      return true;
    }
    taken.socket->SetConnected();
    beginHandshake(std::move(taken));
    return true;
  }

  // `kHandshaking`: read at most the remaining hello bytes so data stays
  // queued.
  IoVecCursor cursor(
      {IoVec{init.hello.data() + init.received, kHelloSize - init.received}});
  const ssize_t n = init.socket->Recv(cursor);
  if (n > 0) init.received += n;

  absl::Status status;
  if (n < 0) {
    status = absl::UnavailableError("failed to read hello");
  } else if (init.received < kHelloSize) {
    if (!(flag & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) return true;
    status = absl::UnavailableError("channel closed during handshake");
  }

  poller_->Unregister(fd);
  ChannelInit taken = std::move(init);
  inits_.erase(it);
  finishInit(std::move(taken), std::move(status));
  return true;
}

void TcpManager::finishInit(ChannelInit init, absl::Status status) {
  const bool outbound = init.expected.has_value();
  std::optional<PeerRail> remote;
  if (status.ok()) {
    if (auto parsed = ParseHello(init.hello.data()); parsed.has_value()) {
      remote = PeerRail{parsed->first, parsed->second};
    } else {
      status = absl::DataLossError("invalid hello");
    }
    if (status.ok() && outbound && *remote != *init.expected) {
      status = absl::FailedPreconditionError(absl::StrFormat(
          "reached %s rail %d", remote->peer.ToString(), remote->rail_id));
    }
  }
  if (status.ok()) {
    auto ch = std::make_unique<TcpChannel>(std::move(init.socket));
    ch->SetPeer(remote->peer);
    on_channel_ready_(remote->peer, init.local_rail_id, remote->rail_id,
                      outbound, std::move(ch));
  } else if (outbound) {
    LOG(WARNING) << "channel init to " << init.expected->peer
                 << " failed: " << status;
    reportConnectFailed(init.expected->peer, init.local_rail_id,
                        init.expected->rail_id, std::move(status));
  } else {
    LOG(WARNING) << "dropping inbound channel: " << status;
  }
}

void TcpManager::reportConnectFailed(const PeerId& peer,
                                     const int32_t local_rail_id,
                                     const int32_t remote_rail_id,
                                     absl::Status status) {
  metrics_.tcp_connect_failures.Add(1);
  on_connect_failed_(peer, local_rail_id, remote_rail_id, std::move(status));
}

void TcpManager::expireInits() {
  const absl::Duration now = util::MonotonicNow();
  for (auto it = inits_.begin(); it != inits_.end();) {
    const auto cur = it++;
    if (cur->second.expiration <= now) {
      poller_->Unregister(cur->first);
      ChannelInit init = std::move(cur->second);
      inits_.erase(cur);
      finishInit(std::move(init),
                 absl::DeadlineExceededError("channel init timed out"));
    }
  }
}

void TcpManager::runLoop() {
  LOG(INFO) << "tcp manager starting with " << listeners_.size()
            << " listeners";
  epoll_event events[kEpollMaxNumEvents];
  while (!isStopped()) {
    drainPending();
    const int nfds =
        poller_->BlockingWait(events, kEpollMaxNumEvents, kEpollWaitTimeoutMs);
    if (isStopped()) break;
    if (nfds > 0) {
      for (int i = 0; i < nfds; ++i) {
        const fd_t fd(events[i].data.fd);
        const uint32_t flag = events[i].events;
        if (fd == event_fd_) {
          uint64_t val = 0;
          while (::read(event_fd_.value(), &val, sizeof(val)) > 0) {
          }
          continue;
        }
        if (handleAccept(fd, flag)) continue;
        if (handleInit(fd, flag)) continue;
        DCHECK(false) << EvtMsg("unhandled", fd, flag);
      }
    } else if (nfds < 0) {
      LOG_EVERY_N_SEC(ERROR, 1) << "poller wait failed";
    }
    expireInits();
  }
  listeners_.clear();
  inits_.clear();
  LOG(INFO) << "tcp manager stopped";
}

}  // namespace peregrine::internal::engine2
