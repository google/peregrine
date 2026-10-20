#include "peregrine/src/api/v1/ip_util.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"

namespace peregrine::v1 {
namespace {
// Splits `peer` ("host:port" or "[v6]:port") into (host, port).
absl::StatusOr<std::pair<std::string, std::string>> SplitHostPort(
    std::string_view peer) {
  if (!peer.empty() && peer.front() == '[') {
    size_t closing_bracket = peer.find(']');
    if (closing_bracket == std::string_view::npos ||
        closing_bracket + 1 >= peer.size() ||
        peer[closing_bracket + 1] != ':') {
      return absl::InvalidArgumentError(
          "Invalid IPv6 peer bracket string format");
    }
    return std::make_pair(std::string(peer.substr(1, closing_bracket - 1)),
                          std::string(peer.substr(closing_bracket + 2)));
  }
  std::vector<std::string> parts = absl::StrSplit(peer, ':');
  if (parts.size() != 2) {
    return absl::InvalidArgumentError("Invalid peer string format");
  }
  return std::make_pair(std::move(parts[0]), std::move(parts[1]));
}

// A numeric IPv4 or IPv6 address in network byte order.
struct RawIpAddress {
  int family = AF_UNSPEC;
  unsigned char bytes[sizeof(in6_addr)] = {};
  size_t len = 0;

  int prefix_bits() const { return static_cast<int>(len * 8); }
  bool IsLoopback() const {
    if (family == AF_INET) return bytes[0] == 127;
    return std::memcmp(bytes, &in6addr_loopback, sizeof(in6_addr)) == 0;
  }
};

absl::StatusOr<RawIpAddress> ParseIpAddress(std::string_view ip) {
  const std::string ip_str(ip);
  RawIpAddress out;
  if (inet_pton(AF_INET, ip_str.c_str(), out.bytes) == 1) {
    out.family = AF_INET;
    out.len = sizeof(in_addr);
    return out;
  }
  if (inet_pton(AF_INET6, ip_str.c_str(), out.bytes) == 1) {
    out.family = AF_INET6;
    out.len = sizeof(in6_addr);
    return out;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("source IP ", ip, " is not a numeric IPv4/IPv6 address"));
}

// Address bytes of a sockaddr of `family` (AF_INET or AF_INET6).
const void* SockaddrAddressBytes(const sockaddr* sa, int family) {
  if (family == AF_INET) {
    return &(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr);
  }
  return &(reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr);
}

// First address of `host` in `family`, or nullopt when `host` resolves but has
// no address in that family.
absl::StatusOr<std::optional<RawIpAddress>> ResolvePeerAddress(
    const std::string& host, const std::string& port, int family) {
  struct addrinfo hints;
  struct addrinfo* result = nullptr;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  // A numeric host resolves without consulting nsswitch; only a hostname
  // (EAI_NONAME under AI_NUMERICHOST) needs the full resolver path.
  hints.ai_flags = AI_NUMERICHOST;
  int ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  if (ret == EAI_NONAME) {
    hints.ai_flags = 0;
    ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  }
  if (ret != 0 || result == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "getaddrinfo failed for host ", host, ": ", gai_strerror(ret)));
  }
  absl::Cleanup free_result = [result] { freeaddrinfo(result); };
  for (const addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
    if (rp->ai_family != family) continue;
    RawIpAddress out;
    out.family = family;
    out.len = family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr);
    std::memcpy(out.bytes, SockaddrAddressBytes(rp->ai_addr, family), out.len);
    return out;
  }
  return std::nullopt;
}

// Index of the local interface that owns `address` (an exact match on one of
// its getifaddrs() addresses). NotFound when no interface owns it.
absl::StatusOr<int> OwningInterfaceIndex(const RawIpAddress& address,
                                         std::string_view source_ip) {
  ifaddrs* ifa_list = nullptr;
  if (getifaddrs(&ifa_list) != 0) {
    return absl::ErrnoToStatus(errno, "getifaddrs failed");
  }
  absl::Cleanup free_list = [ifa_list] { freeifaddrs(ifa_list); };
  for (const ifaddrs* ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr ||
        ifa->ifa_addr->sa_family != address.family) {
      continue;
    }
    if (std::memcmp(SockaddrAddressBytes(ifa->ifa_addr, address.family),
                    address.bytes, address.len) != 0) {
      continue;
    }
    const unsigned int index = if_nametoindex(ifa->ifa_name);
    if (index == 0) {
      return absl::ErrnoToStatus(
          errno, absl::StrCat("if_nametoindex failed for ", ifa->ifa_name,
                              " (owner of ", source_ip, ")"));
    }
    return static_cast<int>(index);
  }
  return absl::NotFoundError(absl::StrCat(
      "source IP ", source_ip, " is not an address of any local interface"));
}

// Appends an rtattr of `type` carrying `len` bytes of `data` to the netlink
// message at `nlh`, which must have room for it.
void AppendRouteAttribute(nlmsghdr* nlh, uint16_t type, const void* data,
                          size_t len) {
  rtattr* rta = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(nlh) +
                                          NLMSG_ALIGN(nlh->nlmsg_len));
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(len);
  std::memcpy(RTA_DATA(rta), data, len);
  nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

// Interface index the kernel would send packets from `source` to `peer`
// through, honouring policy routing (RTM_GETROUTE with RTA_DST and RTA_SRC;
// what `ip route get <peer> from <source>` reports as `dev`).
absl::StatusOr<int> RouteEgressInterfaceIndex(const RawIpAddress& source,
                                              const RawIpAddress& peer,
                                              std::string_view source_ip,
                                              std::string_view peer_str) {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "NETLINK_ROUTE socket failed");
  }
  absl::Cleanup close_fd = [fd] { close(fd); };

  struct {
    nlmsghdr nlh;
    rtmsg rtm;
    // Two address attributes, each RTA_ALIGN(RTA_LENGTH(16)) = 20 bytes.
    unsigned char attrs[64];
  } request = {};
  request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
  request.nlh.nlmsg_type = RTM_GETROUTE;
  request.nlh.nlmsg_flags = NLM_F_REQUEST;
  request.nlh.nlmsg_seq = 1;
  request.rtm.rtm_family = source.family;
  request.rtm.rtm_dst_len = peer.prefix_bits();
  request.rtm.rtm_src_len = source.prefix_bits();
  AppendRouteAttribute(&request.nlh, RTA_DST, peer.bytes, peer.len);
  AppendRouteAttribute(&request.nlh, RTA_SRC, source.bytes, source.len);

  if (send(fd, &request, request.nlh.nlmsg_len, 0) < 0) {
    return absl::ErrnoToStatus(
        errno, absl::StrCat("RTM_GETROUTE send failed for route from ",
                            source_ip, " to ", peer_str));
  }

  unsigned char reply[8192];
  ssize_t reply_len = 0;
  do {
    reply_len = recv(fd, reply, sizeof(reply), 0);
  } while (reply_len < 0 && errno == EINTR);
  if (reply_len < 0) {
    return absl::ErrnoToStatus(
        errno, absl::StrCat("RTM_GETROUTE recv failed for route from ",
                            source_ip, " to ", peer_str));
  }

  int remaining = static_cast<int>(reply_len);
  for (nlmsghdr* nlh = reinterpret_cast<nlmsghdr*>(reply);
       NLMSG_OK(nlh, remaining); nlh = NLMSG_NEXT(nlh, remaining)) {
    if (nlh->nlmsg_type == NLMSG_ERROR) {
      const nlmsgerr* err = static_cast<const nlmsgerr*>(NLMSG_DATA(nlh));
      if (err->error == 0) continue;
      return absl::ErrnoToStatus(
          -err->error, absl::StrCat("kernel route lookup from ", source_ip,
                                    " to ", peer_str, " failed"));
    }
    if (nlh->nlmsg_type != RTM_NEWROUTE) continue;
    const rtmsg* rtm = static_cast<const rtmsg*>(NLMSG_DATA(nlh));
    int attr_len = static_cast<int>(RTM_PAYLOAD(nlh));
    for (const rtattr* rta = RTM_RTA(rtm); RTA_OK(rta, attr_len);
         rta = RTA_NEXT(rta, attr_len)) {
      if (rta->rta_type != RTA_OIF) continue;
      int oif = 0;
      std::memcpy(&oif, RTA_DATA(rta), sizeof(oif));
      return oif;
    }
    return absl::InternalError(absl::StrCat("kernel route reply from ",
                                            source_ip, " to ", peer_str,
                                            " has no RTA_OIF"));
  }
  return absl::InternalError(absl::StrCat(
      "no RTM_NEWROUTE reply for route from ", source_ip, " to ", peer_str));
}

// Uncached evaluation behind SourceIpRoutesToPeer.
absl::StatusOr<bool> ComputeSourceIpRoutesToPeer(std::string_view source_ip,
                                                 std::string_view peer) {
  ABSL_ASSIGN_OR_RETURN(const auto host_port, SplitHostPort(peer));
  ABSL_ASSIGN_OR_RETURN(const RawIpAddress source, ParseIpAddress(source_ip));
  ABSL_ASSIGN_OR_RETURN(
      const std::optional<RawIpAddress> peer_address,
      ResolvePeerAddress(host_port.first, host_port.second, source.family));
  if (!peer_address.has_value()) {
    // A source of one family cannot reach a peer that only has the other.
    return false;
  }
  if (source.IsLoopback() && peer_address->IsLoopback()) {
    return true;
  }
  ABSL_ASSIGN_OR_RETURN(const int owning_ifindex,
                        OwningInterfaceIndex(source, source_ip));
  ABSL_ASSIGN_OR_RETURN(
      const int egress_ifindex,
      RouteEgressInterfaceIndex(source, *peer_address, source_ip, peer));
  return egress_ifindex == owning_ifindex;
}

// Memoized SourceIpRoutesToPeer results keyed by (source_ip, peer) as passed.
// Host routing is assumed static for the lifetime of the process.
using SourceIpRouteCacheKey = std::pair<std::string, std::string>;
struct SourceIpRouteCache {
  absl::Mutex mu;
  absl::flat_hash_map<SourceIpRouteCacheKey, bool> routes ABSL_GUARDED_BY(mu);
};

SourceIpRouteCache& GetSourceIpRouteCache() {
  static absl::NoDestructor<SourceIpRouteCache> cache;
  return *cache;
}
}  // namespace

absl::StatusOr<bool> SourceIpRoutesToPeer(std::string_view source_ip,
                                          std::string_view peer) {
  SourceIpRouteCache& cache = GetSourceIpRouteCache();
  const SourceIpRouteCacheKey key{std::string(source_ip), std::string(peer)};
  {
    absl::MutexLock lock(cache.mu);
    const auto it = cache.routes.find(key);
    if (it != cache.routes.end()) return it->second;
  }
  // Computed outside the lock: getifaddrs() and netlink are slow and the
  // result is a pure function of (key, host routing state), so a concurrent
  // duplicate computation is harmless.
  ABSL_ASSIGN_OR_RETURN(const bool routes_to_peer,
                        ComputeSourceIpRoutesToPeer(source_ip, peer));
  absl::MutexLock lock(cache.mu);
  cache.routes.try_emplace(key, routes_to_peer);
  return routes_to_peer;
}

void TestOnly_ClearRouteCache() {
  SourceIpRouteCache& cache = GetSourceIpRouteCache();
  absl::MutexLock lock(cache.mu);
  cache.routes.clear();
}

size_t TestOnly_RouteCacheSize() {
  SourceIpRouteCache& cache = GetSourceIpRouteCache();
  absl::MutexLock lock(cache.mu);
  return cache.routes.size();
}

}  // namespace peregrine::v1
