#include "src/internal/socket/socket_udp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "src/api/types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_util.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

std::unique_ptr<UdpSocket> UdpSocket::Create(int family) {
  constexpr bool kNonblocking = false;
  const int fd = CreateSocket(family, SOCK_DGRAM, kNonblocking);
  if ABSL_PREDICT_FALSE (fd < 0) {
    return nullptr;
  } else {
    LOG(INFO) << okMsg("created", fd);
    return absl::WrapUnique(new UdpSocket(fd, family));
  }
}

UdpSocket::~UdpSocket() {
  DCHECK(invariant());
  LOG(INFO) << okMsg("closing");
  connected_ = false;
  ::close(fd_);
}

namespace {
auto BindV4(int fd, const Endpoint& local) {
  const struct sockaddr_in sa = local.BuildIPv4Sockaddr();
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto BindV6(int fd, const Endpoint& local) {
  const struct sockaddr_in6 sa = local.BuildIPv6Sockaddr();
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV4(int fd, const Endpoint& peer) {
  const struct sockaddr_in sa = peer.BuildIPv4Sockaddr();
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV6(int fd, const Endpoint& peer) {
  const struct sockaddr_in6 sa = peer.BuildIPv6Sockaddr();
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}
}  // namespace

bool UdpSocket::Bind(const Endpoint& local) const {
  const auto bind = local.IsIPv4() ? BindV4 : BindV6;
  if ABSL_PREDICT_FALSE (bind(fd_, local) < 0) {
    LOG(WARNING) << errMsg("bind");
    return false;
  } else {
    LOG(INFO) << okMsg("bound");
    return true;
  }
}

bool UdpSocket::Connect(const Endpoint& peer) {
  const auto connect = peer.IsIPv4() ? ConnectV4 : ConnectV6;
  if ABSL_PREDICT_FALSE (connect(fd_, peer) < 0) {
    LOG(WARNING) << errMsg("connect");
    return false;
  } else {
    LOG(INFO) << okMsg("connected");
    connected_ = true;
    return true;
  }
}

bool UdpSocket::Send(const Byte* const buf, const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);

  const ssize_t bytes = ::send(fd_, buf, len, /*flags=*/0);
  DCHECK(bytes == len || bytes < 0);
  if ABSL_PREDICT_TRUE (bytes == len) {
    VLOG(1) << ioMsg("send", bytes);
    return true;
  } else if (Interrupted()) {
    return false;
  } else {
    LOG(WARNING) << errMsg("send");
    return false;
  }
}

ssize_t UdpSocket::Recv(Byte* const buf, const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);

  const ssize_t bytes = ::recv(fd_, buf, len, /*flags=*/0);
  DCHECK_LE(bytes, len);
  if ABSL_PREDICT_TRUE (bytes > 0) {
    VLOG(1) << ioMsg("recv", bytes);
    return bytes;
  } else if (bytes < 0) {
    if (Interrupted()) return 0;
    LOG(WARNING) << errMsg("recv");
    return -1;
  } else {
    DCHECK_EQ(bytes, 0);  // zero-length payload
    LOG(INFO) << ioMsg("recv", 0);
    return 0;
  }
}

bool UdpSocket::SendV(const IoVec* const iov, const int n,
                      const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);
  DCHECK_EQ(TotalLength(iov, n), len);

  const ssize_t bytes = ::writev(fd_, iov, n);
  DCHECK(bytes == len || bytes < 0);
  if ABSL_PREDICT_TRUE (bytes == len) {
    VLOG(1) << ioMsg("writev", bytes);
    return true;
  } else if (Interrupted()) {
    return false;
  } else {
    LOG(WARNING) << errMsg("writev");
    return false;
  }
}

ssize_t UdpSocket::RecvV(const IoVec* const iov, const int n,
                         const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);
  DCHECK_EQ(TotalLength(iov, n), len);

  const ssize_t bytes = ::readv(fd_, iov, n);
  DCHECK_LE(bytes, len);
  if ABSL_PREDICT_TRUE (bytes > 0) {
    VLOG(1) << ioMsg("readv", bytes);
    return bytes;
  } else if (bytes < 0) {
    if (Interrupted()) return 0;
    LOG(WARNING) << errMsg("readv");
    return -1;
  } else {
    DCHECK_EQ(bytes, 0);  // zero-length payload
    LOG(INFO) << ioMsg("readv", 0);
    return 0;
  }
}

std::string UdpSocket::ToString() const {
  return absl::StrCat("udp socket: ", AddrPortPair(fd_));
}

}  // namespace peregrine::internal
