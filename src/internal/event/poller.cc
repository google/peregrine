#include "src/internal/event/poller.h"

#include <sys/epoll.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

namespace {
std::string OkMsg(std::string_view what, fd_t fd) {
  return absl::StrCat("event poller ", what, ", fd=", fd.value());
}

std::string ErrMsg(const std::string_view what, const int last_errno) {
  return absl::StrFormat("epoll %s failed: errno=%d (%s)", what, last_errno,
                         std::strerror(last_errno));
}
}  // namespace

std::unique_ptr<Poller> Poller::Create() {
  const int ret = ::epoll_create1(/*flags=*/EPOLL_CLOEXEC);
  if ABSL_PREDICT_FALSE (ret < 0) {
    const auto last_errno = errno;
    LOG(ERROR) << ErrMsg("create", last_errno);
    return nullptr;
  }
  const fd_t epoll_fd(ret);
  LOG(INFO) << OkMsg("created", epoll_fd);
  return absl::WrapUnique(new Poller(epoll_fd));
}

Poller::~Poller() {
  DCHECK(invariant());
  ::close(epoll_fd_.value());
  LOG(INFO) << OkMsg("destroyed", epoll_fd_);
}

bool Poller::Register(const fd_t fd, const uint32_t events) {
  struct epoll_event ev = {
      .events = events,
      .data = {.fd = fd.value()},
  };
  if (::epoll_ctl(epoll_fd_.value(), EPOLL_CTL_ADD, fd.value(), &ev) < 0) {
    const auto last_errno = errno;
    LOG(ERROR) << ErrMsg("register", last_errno);
    return false;
  }
  LOG(INFO) << OkMsg("registered", fd);
  return true;
}

bool Poller::Unregister(const fd_t fd) {
  if (::epoll_ctl(epoll_fd_.value(), EPOLL_CTL_DEL, fd.value(), nullptr) < 0) {
    const auto last_errno = errno;
    LOG(ERROR) << ErrMsg("unregister", last_errno);
    return false;
  }
  LOG(INFO) << OkMsg("unregistered", fd);
  return true;
}

int Poller::BlockingWait(epoll_event* events, int max_events) {
  DCHECK_NE(events, nullptr);
  DCHECK_GE(max_events, 1);

  const int efd = epoll_fd_.value();
  constexpr int kInfiniteTimeout = -1;
  const int nfds = ::epoll_wait(efd, events, max_events, kInfiniteTimeout);
  if ABSL_PREDICT_FALSE (nfds < 0) {
    const auto last_errno = errno;
    LOG(ERROR) << ErrMsg("blocking wait", last_errno);
  }
  return nfds;
}

}  // namespace peregrine::internal
