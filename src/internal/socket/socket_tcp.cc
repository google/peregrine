#include "src/internal/socket/socket_tcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine {

constexpr absl::string_view kTcpPrefix = "tcp socket: ";

namespace {
std::string Success(int fd, absl::string_view msg) {
  return absl::StrFormat("%s%s fd=%d %s", kTcpPrefix, msg, fd,
                         AddrPortPair(fd));
}

std::string Error(absl::string_view msg) {
  return absl::StrFormat("%s%s errno=%d(%s)", kTcpPrefix, msg, errno,
                         std::strerror(errno));
}

absl::Status InternalError(absl::string_view msg) {
  return absl::InternalError(Error(msg));
}

absl::Status AbortedError(absl::string_view msg) {
  return absl::AbortedError(Error(msg));
}
}  // namespace

absl::StatusOr<std::unique_ptr<TcpSocket>> TcpSocket::Create(int family) {
  constexpr bool kNonblocking = false;
  const auto maybe_fd = CreateSocket(family, SOCK_STREAM, kNonblocking);
  if (!maybe_fd.ok()) {
    return maybe_fd.status();
  } else {
    const int fd = maybe_fd.value();
    LOG(INFO) << Success(fd, "created");
    return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/false));
  }
}

std::unique_ptr<TcpSocket> TcpSocket::Create(int fd, int family) {
  return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/true));
}

TcpSocket::~TcpSocket() {
  DCHECK(invariant());
  LOG(INFO) << Success(fd_, "closing");
  ::shutdown(fd_, SHUT_RDWR);  // discards unread data
  connected_ = false;
  ::close(fd_);
}

namespace {
auto BindV4(int fd, ipaddr_t ip, port_t port) {
  const struct sockaddr_in sa = BuildIPv4Sockaddr(ip, port);
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto BindV6(int fd, ipaddr_t ip, port_t port) {
  const struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, port);
  return ::bind(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV4(int fd, ipaddr_t ip, port_t port) {
  const struct sockaddr_in sa = BuildIPv4Sockaddr(ip, port);
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto ConnectV6(int fd, ipaddr_t ip, port_t port) {
  const struct sockaddr_in6 sa = BuildIPv6Sockaddr(ip, port);
  return ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
}

auto AcceptV4(int fd) {
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  return ::accept4(fd, (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
}

auto AcceptV6(int fd) {
  struct sockaddr_in6 sa;
  socklen_t len = sizeof(sa);
  return ::accept4(fd, (struct sockaddr*)&sa, &len, SOCK_CLOEXEC);
}
}  // namespace

absl::Status TcpSocket::Listen(ipaddr_t ip, port_t port) const {
  int opt = 1;  // enable
  if (const auto status = SetOption(fd_, SO_REUSEADDR, &opt, sizeof(opt));
      !status.ok()) {
    return status;
  }
  const auto bind = IsIPv4Addr(ip) ? BindV4 : BindV6;
  if (bind(fd_, ip, port) < 0) {
    return InternalError("bind");
  } else if (::listen(fd_, SOMAXCONN) < 0) {
    return InternalError("listen");
  } else {
    LOG(INFO) << Success(fd_, "listening");
    return absl::OkStatus();
  }
}

absl::StatusOr<int> TcpSocket::Accept() const {
  const auto accept = family_ == AF_INET ? AcceptV4 : AcceptV6;
  if (const int new_fd = accept(fd_); new_fd < 0) {
    return InternalError("accept");
  } else {
    DCHECK_GE(new_fd, 0);
    LOG(INFO) << Success(new_fd, "accepted");
    return new_fd;
  }
}

absl::Status TcpSocket::Connect(ipaddr_t ip, port_t port) {
  const auto connect = IsIPv4Addr(ip) ? ConnectV4 : ConnectV6;
  if (connect(fd_, ip, port) < 0) {
    return InternalError("connect");
  } else {
    LOG(INFO) << Success(fd_, "connected");
    connected_ = true;
    return absl::OkStatus();
  }
}

absl::Status TcpSocket::Send(const Byte* const buf, const size_t len) const {
  DCHECK_GE(len, 1);
  const Byte* ptr = buf;
  size_t sent = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::send(fd_, ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      sent += bytes;
      DCHECK_EQ(buf + len, ptr + left);
    } else if (bytes < 0) {
      if (Interrupted()) continue;
      return InternalError("send");
    } else {  // rarely happens
      DCHECK_EQ(bytes, 0);
      return AbortedError("send connection closed");
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(sent, len);
  return absl::OkStatus();
}

absl::Status TcpSocket::Recv(Byte* const buf, const size_t len) const {
  DCHECK_GE(len, 1);
  Byte* ptr = buf;
  size_t rcvd = 0;
  ssize_t left = len;
  while (left > 0) {
    const ssize_t bytes = ::recv(fd_, ptr, left, /*flags=*/0);
    if ABSL_PREDICT_TRUE (bytes > 0) {
      DCHECK_LE(bytes, left);
      ptr += bytes;
      left -= bytes;
      rcvd += bytes;
      DCHECK_EQ(buf + len, ptr + left);
    } else if (bytes < 0) {
      if (Interrupted()) continue;
      return InternalError("recv");
    } else {
      DCHECK_EQ(bytes, 0);  // peer closed connection
      return AbortedError("recv eof");
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(rcvd, len);
  return absl::OkStatus();
}

std::string TcpSocket::ToString() const {
  return absl::StrCat(kTcpPrefix, AddrPortPair(fd_));
}

}  // namespace peregrine
