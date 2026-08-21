#include "src/util/nic.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace peregrine::util {

namespace {
std::string Error(absl::string_view msg, int last_errno) {
  return absl::StrFormat("%s errno=%d(%s)", msg, last_errno,
                         std::strerror(last_errno));
}
}  // namespace

absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics() {
  absl::flat_hash_map<std::string, std::vector<std::string>> nics;

  // Retrieve the interfaces list.
  struct ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) < 0 || interfaces == nullptr) {
    const auto last_errno = errno;
    LOG(WARNING) << Error("getifaddrs", last_errno);
    return nics;
  }

  // Collect the IP addresses.
  DCHECK_NE(interfaces, nullptr);
  for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    const struct sockaddr* sa = ifa->ifa_addr;
    if (sa == nullptr) continue;

    const int family = sa->sa_family;
    if (family != AF_INET && family != AF_INET6) continue;

    char ip[NI_MAXHOST];
    std::memset(ip, 0, sizeof(ip));
    constexpr int kFlags = NI_NUMERICHOST;
    const size_t salen = (family == AF_INET) ? sizeof(struct sockaddr_in)
                                             : sizeof(struct sockaddr_in6);
    if (getnameinfo(sa, salen, ip, sizeof(ip), nullptr, 0, kFlags) != 0) {
      continue;
    }

    // "fe80::812f:1ecb:8970:a1d4%ens4" -> "fe80::812f:1ecb:8970:a1d4"
    if (char* p = std::strchr(ip, '%'); p != nullptr) *p = '\0';

    // "lo - 127.0.0.1", "lo - ::1"
    // "ens4 - 172.19.255.221", "ens4 - fe80::812f:1ecb:8970:a1d4"
    if (const char* name = ifa->ifa_name; name != nullptr) {
      nics[name].push_back(ip);
    }
  }

  freeifaddrs(interfaces);
  return nics;
}

}  // namespace peregrine::util
