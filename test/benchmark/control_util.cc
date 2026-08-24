#include "test/benchmark/control_util.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "test/benchmark/control.pb.h"

namespace peregrine::benchmark {

bool SendControlMessage(int fd, const proto::ControlMessage& msg) {
  std::string data;
  if (!msg.SerializeToString(&data)) return false;
  uint32_t len = htonl(data.size());
  return write(fd, &len, 4) == 4 && write(fd, data.data(), data.size()) ==
                                        static_cast<ssize_t>(data.size());
}

bool ProcessControlMessage(int fd, proto::ControlMessage* msg) {
  uint32_t len = 0;
  if (read(fd, &len, 4) != 4) return false;
  std::string data(ntohl(len), '\0');
  return read(fd, data.data(), data.size()) ==
             static_cast<ssize_t>(data.size()) &&
         msg->ParseFromString(data);
}

namespace {
bool IsIPv6(std::string_view host) { return absl::StrContains(host, ':'); }
}  // namespace

int CreateControlListener(std::string_view ip, uint16_t port) {
  const std::string ip_str(ip);
  const bool ipv6 = IsIPv6(ip);
  int fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  CHECK_GE(fd, 0) << "Failed to create control listener socket";

  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    LOG(WARNING) << "Failed to set SO_REUSEADDR";
  }

  if (ipv6) {
    struct sockaddr_in6 addr = {.sin6_family = AF_INET6,
                                .sin6_port = htons(port)};
    CHECK_EQ(inet_pton(AF_INET6, ip_str.c_str(), &addr.sin6_addr), 1)
        << "Failed to parse IPv6 address: " << ip;
    CHECK_EQ(bind(fd, (struct sockaddr*)&addr, sizeof(addr)), 0)
        << "Failed to bind control listener to " << ip << ":" << port;
  } else {
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
    CHECK_EQ(inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr), 1)
        << "Failed to parse IPv4 address: " << ip;
    CHECK_EQ(bind(fd, (struct sockaddr*)&addr, sizeof(addr)), 0)
        << "Failed to bind control listener to " << ip << ":" << port;
  }

  CHECK_EQ(listen(fd, 1), 0) << "Failed to listen on control socket";
  return fd;
}

int ConnectControlWithRetry(std::string_view host, uint16_t port) {
  const std::string host_str(host);
  const bool ipv6 = IsIPv6(host);
  const absl::Time deadline = absl::Now() + absl::Seconds(60);

  if (ipv6) {
    struct sockaddr_in6 addr = {.sin6_family = AF_INET6,
                                .sin6_port = htons(port)};
    CHECK_EQ(inet_pton(AF_INET6, host_str.c_str(), &addr.sin6_addr), 1)
        << "Failed to parse IPv6 address: " << host;

    while (true) {
      int fd = socket(AF_INET6, SOCK_STREAM, 0);
      if (fd >= 0) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        close(fd);
      }
      if (absl::Now() >= deadline) break;
      absl::SleepFor(absl::Milliseconds(500));
    }
  } else {
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
    CHECK_EQ(inet_pton(AF_INET, host_str.c_str(), &addr.sin_addr), 1)
        << "Failed to parse IPv4 address: " << host;

    while (true) {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd >= 0) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        close(fd);
      }
      if (absl::Now() >= deadline) break;
      absl::SleepFor(absl::Milliseconds(500));
    }
  }
  LOG(FATAL) << "Timeout connecting to control " << host << ":" << port;
}

}  // namespace peregrine::benchmark
