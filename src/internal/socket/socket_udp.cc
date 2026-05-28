#include "src/internal/socket/socket_udp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"
#include "src/internal/socket/socket_util.h"
#include "src/internal/util/util.h"

namespace peregrine::internal {

constexpr std::string_view kUdpPrefix = "udp socket: ";

namespace {
std::string Success(int fd, std::string_view msg) {
  return absl::StrFormat("%s%s fd=%d %s", kUdpPrefix, msg, fd,
                         AddrPortPair(fd));
}

std::string Error(std::string_view msg) {
  return absl::StrFormat("%s%s errno=%d(%s)", kUdpPrefix, msg, errno,
                         std::strerror(errno));
}

absl::Status InternalError(std::string_view msg) {
  return absl::InternalError(Error(msg));
}
}  // namespace

absl::StatusOr<std::unique_ptr<UdpSocket>> UdpSocket::Create(int family) {
  constexpr bool kNonblocking = false;
  const auto maybe_fd = CreateSocket(family, SOCK_DGRAM, kNonblocking);
  if (!maybe_fd.ok()) {
    return maybe_fd.status();
  } else {
    const int fd = maybe_fd.value();
    LOG(INFO) << Success(fd, "created");
    return absl::WrapUnique(new UdpSocket(fd, family));
  }
}

UdpSocket::~UdpSocket() {
  DCHECK(invariant());
  LOG(INFO) << Success(fd_, "closing");
  ::shutdown(fd_, SHUT_RDWR);  // discards unread data
  connected_ = false;
  ::close(fd_);
}

namespace {
auto BindV4(int fd, const IpAddr& ip, port_t port) {
  const struct sockaddr_in sa = BuildIPv4Sockaddr(ip, port);
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto BindV6(int fd, const IpAddr& ip, port_t port) {
  const struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, port);
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV4(int fd, const IpAddr& ip, port_t port) {
  const struct sockaddr_in sa = BuildIPv4Sockaddr(ip, port);
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV6(int fd, const IpAddr& ip, port_t port) {
  const struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, port);
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}
}  // namespace

absl::Status UdpSocket::Bind(const IpAddr& ip, port_t port) const {
  const auto bind = IsIPv4(ip) ? BindV4 : BindV6;
  if (bind(fd_, ip, port) < 0) {
    return InternalError("bind");
  } else {
    LOG(INFO) << Success(fd_, "bound");
    return absl::OkStatus();
  }
}

absl::Status UdpSocket::Connect(const IpAddr& ip, port_t port) {
  const auto connect = IsIPv4(ip) ? ConnectV4 : ConnectV6;
  if (connect(fd_, ip, port) < 0) {
    return InternalError("connect");
  } else {
    LOG(INFO) << Success(fd_, "connected");
    connected_ = true;
    return absl::OkStatus();
  }
}

absl::Status UdpSocket::Send(const Byte* const buf, const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);

  const ssize_t bytes = ::send(fd_, buf, len, /*flags=*/0);
  DCHECK(bytes == len || bytes < 0);
  if ABSL_PREDICT_TRUE (bytes == len) {
    return absl::OkStatus();
  } else {
    return InternalError("send");
  }
}

absl::StatusOr<size_t> UdpSocket::Recv(Byte* const buf,
                                       const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);

  const ssize_t bytes = ::recv(fd_, buf, len, /*flags=*/0);
  DCHECK_LE(bytes, len);
  if ABSL_PREDICT_TRUE (bytes > 0) {
    return bytes;
  } else if (bytes < 0) {
    if (Interrupted()) return 0;
    return InternalError("recv");
  } else {
    DCHECK_EQ(bytes, 0);  // zero-length payload
    return 0;
  }
}

absl::Status UdpSocket::SendV(const IoVec* const iov, const int n,
                              const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);
  DCHECK_EQ(TotalLength(iov, n), len);

  const ssize_t bytes = ::writev(fd_, iov, n);
  DCHECK(bytes == len || bytes < 0);
  if ABSL_PREDICT_TRUE (bytes == len) {
    return absl::OkStatus();
  } else {
    return InternalError("send");
  }
}

absl::StatusOr<size_t> UdpSocket::RecvV(const IoVec* const iov, const int n,
                                        const size_t len) const {
  DCHECK(connected_);
  DCHECK_GE(len, 1);
  DCHECK_EQ(TotalLength(iov, n), len);

  const ssize_t bytes = ::readv(fd_, iov, n);
  DCHECK_LE(bytes, len);
  if ABSL_PREDICT_TRUE (bytes > 0) {
    return bytes;
  } else if (bytes < 0) {
    if (Interrupted()) return 0;
    return InternalError("recv");
  } else {
    DCHECK_EQ(bytes, 0);  // zero-length payload
    return 0;
  }
}

std::string UdpSocket::ToString() const {
  return absl::StrCat(kUdpPrefix, AddrPortPair(fd_));
}

}  // namespace peregrine::internal
