#include "src/internal/socket/socket_tcp.h"

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
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpSocket::Create(int family) {
  constexpr bool kNonblocking = false;
  const int fd = CreateSocket(family, SOCK_STREAM, kNonblocking);
  if (fd < 0) {
    return nullptr;
  } else {
    LOG(INFO) << successMsg("create", fd);
    return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/false));
  }
}

std::unique_ptr<TcpSocket> TcpSocket::Create(int fd, int family) {
  return absl::WrapUnique(new TcpSocket(fd, family, /*connected=*/true));
}

TcpSocket::~TcpSocket() {
  DCHECK(invariant());
  LOG(INFO) << successMsg("shutdown");
  ::shutdown(fd_, SHUT_RDWR);  // discards unread data
  connected_ = false;
  LOG(INFO) << successMsg("close");
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

bool TcpSocket::Listen(const IpAddr& ip, port_t port) const {
  int opt = 1;  // enable
  if (!SetOption(fd_, SO_REUSEADDR, &opt, sizeof(opt))) {
    LOG(WARNING) << errorMsg("set SO_REUSEADDR");
    return false;
  }
  const auto bind = IsIPv4(ip) ? BindV4 : BindV6;
  if (bind(fd_, ip, port) < 0) {
    LOG(WARNING) << errorMsg("bind");
    return false;
  } else if (::listen(fd_, SOMAXCONN) < 0) {
    LOG(WARNING) << errorMsg("listen");
    return false;
  } else {
    LOG(INFO) << successMsg("listening on");
    return true;
  }
}

int TcpSocket::Accept() const {
  const auto accept = family_ == AF_INET ? AcceptV4 : AcceptV6;
  if (const int new_fd = accept(fd_); new_fd < 0) {
    LOG(WARNING) << errorMsg("accept");
    return -1;
  } else {
    DCHECK_GE(new_fd, 0);
    LOG(INFO) << successMsg("accepted", new_fd);
    return new_fd;
  }
}

bool TcpSocket::Connect(const IpAddr& ip, port_t port) {
  const auto connect = IsIPv4(ip) ? ConnectV4 : ConnectV6;
  if (connect(fd_, ip, port) < 0) {
    LOG(WARNING) << errorMsg("connect");
    return false;
  } else {
    LOG(INFO) << successMsg("connected");
    connected_ = true;
    return true;
  }
}

bool TcpSocket::Send(const Byte* const buf, const size_t len) const {
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
      LOG(WARNING) << errorMsg("send");
      return false;
    } else {  // rarely happens
      DCHECK_EQ(bytes, 0);
      LOG(WARNING) << errorMsg("send zero");
      return false;
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(sent, len);
  return true;
}

bool TcpSocket::Recv(Byte* const buf, const size_t len) const {
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
      LOG(WARNING) << errorMsg("recv");
      return false;
    } else {
      DCHECK_EQ(bytes, 0);  // peer closed connection
      LOG(INFO) << errorMsg("recv eof");
      return false;
    }
  }
  DCHECK_EQ(left, 0);
  DCHECK_EQ(rcvd, len);
  return true;
}

std::string TcpSocket::ToString() const {
  return absl::StrCat("tcp socket: ", AddrPortPair(fd_));
}

}  // namespace peregrine::internal
