#include "peregrine/src/internal/routing/route.h"

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
#include <cstdint>
#include <cstring>
#include <optional>

#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal {
namespace {

// Returns the index (>= 1) of the local interface that owns `ip` address.
std::optional<int> OwningInterface(const util::IpAddr& ip) {
  // Retrieve the interfaces list.
  struct ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) < 0 || interfaces == nullptr) {
    return std::nullopt;
  }

  // Free the interfaces list when done.
  absl::Cleanup cleanup = [interfaces] { freeifaddrs(interfaces); };

  // Collect the interface index for the given IP address.
  DCHECK_NE(interfaces, nullptr);
  for (struct ifaddrs* ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
    const struct sockaddr* sa = ifa->ifa_addr;
    if (sa == nullptr) continue;

    const int family = sa->sa_family;
    if (family != ip.AddressFamily()) continue;
    if (util::IpAddr::Create(*sa) != ip) continue;

    // if_nametoindex() returns 0 if it fails to find or resolve the nic.
    const auto index = if_nametoindex(ifa->ifa_name);
    if (index <= 0) return std::nullopt;
    return index;
  }
  return std::nullopt;
}

// Appends an rtattr of `type` carrying `ip` address to the netlink message
// at `nlh`, which must have room for it.
void AppendRouteAttr(nlmsghdr* nlh, uint16_t type, const util::IpAddr& ip) {
  const uint16_t len = static_cast<uint16_t>(ip.Length());
  rtattr* rta = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(nlh) +
                                          NLMSG_ALIGN(nlh->nlmsg_len));
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(len);
  std::memcpy(RTA_DATA(rta), ip.Data(), len);
  nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

// Returns the interface index the kernel would send packets from `src` to
// `dst` through, honouring policy routing (RTM_GETROUTE with RTA_DST and
// RTA_SRC; what `ip route get <dst> from <src>` reports as `dev`.
std::optional<int> EgressInterface(const util::IpAddr& src,
                                   const util::IpAddr& dst) {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if ABSL_PREDICT_FALSE (fd < 0) return std::nullopt;

  // Close the socket when done.
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
  request.rtm.rtm_family = src.AddressFamily();
  request.rtm.rtm_src_len = src.Length() * 8;  // bits
  request.rtm.rtm_dst_len = dst.Length() * 8;  // bits
  AppendRouteAttr(&request.nlh, RTA_SRC, src);
  AppendRouteAttr(&request.nlh, RTA_DST, dst);

  if (send(fd, &request, request.nlh.nlmsg_len, 0) < 0) return std::nullopt;

  unsigned char reply[8192];
  ssize_t reply_len = 0;
  do {
    reply_len = recv(fd, reply, sizeof(reply), 0);
  } while (reply_len < 0 && errno == EINTR);

  if (reply_len < 0) return std::nullopt;

  int remaining = static_cast<int>(reply_len);
  for (nlmsghdr* nlh = reinterpret_cast<nlmsghdr*>(reply);
       NLMSG_OK(nlh, remaining); nlh = NLMSG_NEXT(nlh, remaining)) {
    if (nlh->nlmsg_type == NLMSG_ERROR) {
      const nlmsgerr* err = static_cast<const nlmsgerr*>(NLMSG_DATA(nlh));
      if (err->error == 0) continue;
      return std::nullopt;
    }
    if (nlh->nlmsg_type != RTM_NEWROUTE) continue;
    const rtmsg* rtm = static_cast<const rtmsg*>(NLMSG_DATA(nlh));
    int attr_len = static_cast<int>(RTM_PAYLOAD(nlh));
    for (const rtattr* rta = RTM_RTA(rtm); RTA_OK(rta, attr_len);
         rta = RTA_NEXT(rta, attr_len)) {
      if (rta->rta_type == RTA_OIF) {
        int oif = 0;
        std::memcpy(&oif, RTA_DATA(rta), sizeof(oif));
        return oif;
      }
    }
    return std::nullopt;
  }
  return std::nullopt;
}
}  // namespace

/*static*/ bool Route::Compute(const util::IpAddr& src,
                               const util::IpAddr& dst) {
  if (src.IsLoopback()) return dst.IsLoopback();
  if (dst.IsLoopback()) return src.IsLoopback();

  const std::optional<int> oi = OwningInterface(src);
  if (!oi.has_value()) return false;

  const std::optional<int> ei = EgressInterface(src, dst);
  if (!ei.has_value()) return false;

  return oi.value() == ei.value();
}

bool Route::FindOrInsert(const util::IpAddr& src, const util::IpAddr& dst) {
  const Key key = {src, dst};
  {
    absl::MutexLock _(mu_);
    const auto it = routes_.find(key);
    if (it != routes_.end()) return it->second;
  }
  const bool routable = Compute(src, dst);  // This can be slow!
  {
    absl::MutexLock _(mu_);
    auto [it, inserted] = routes_.try_emplace(key, routable);
    return it->second;
  }
}

}  // namespace peregrine::internal
