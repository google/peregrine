#include "peregrine/src/internal/routing/route.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal {
namespace {

struct RouteRequest {
  struct nlmsghdr nlh;
  struct rtmsg rtm;
  unsigned char attrs[64];
  static_assert(sizeof(attrs) >= 2 * RTA_SPACE(sizeof(util::ipv6_t)));
};

// Appends an rtattr of `type` carrying `ip` address to the netlink message
// at `nlh`, which must have room for it.
void AppendRouteAttr(struct nlmsghdr* const nlh, const uint16_t type,
                     const util::IpAddr& ip) {
  const auto [addr, len] = ip.RawAddrAndLen();
  char* p = reinterpret_cast<char*>(nlh) + NLMSG_ALIGN(nlh->nlmsg_len);
  rtattr* rta = reinterpret_cast<rtattr*>(p);
  rta->rta_type = type;
  rta->rta_len = RTA_LENGTH(len);
  std::memcpy(RTA_DATA(rta), addr, len);
  nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

// Returns a netlink request to get the route for `src` to `dst`.
RouteRequest Build(const util::IpAddr& src, const util::IpAddr& dst) {
  const size_t src_prefix_bits = src.RawAddrAndLen().second * 8;
  const size_t dst_prefix_bits = dst.RawAddrAndLen().second * 8;
  struct RouteRequest r = {};
  r.nlh = {
      .nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg)),
      .nlmsg_type = RTM_GETROUTE,
      .nlmsg_flags = NLM_F_REQUEST,
      .nlmsg_seq = 1,
  };
  r.rtm = {
      .rtm_family = static_cast<uint8_t>(src.AddressFamily()),
      .rtm_dst_len = static_cast<uint8_t>(dst_prefix_bits),
      .rtm_src_len = static_cast<uint8_t>(src_prefix_bits),
  };
  AppendRouteAttr(&r.nlh, RTA_SRC, src);
  AppendRouteAttr(&r.nlh, RTA_DST, dst);
  return r;
}

// Returns the interface index the kernel would send packets from `src` to
// `dst` through, honouring policy routing (RTM_GETROUTE with RTA_DST and
// RTA_SRC; what `ip route get <dst> from <src>` reports as `dev`.
std::optional<int> FindEgressNic(const util::IpAddr& src,
                                 const util::IpAddr& dst) {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) return std::nullopt;
  absl::Cleanup close_fd = [fd] { close(fd); };

  const RouteRequest req = Build(src, dst);
  if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) return std::nullopt;

  unsigned char reply[8192];
  ssize_t reply_len = 0;
  do {
    // MSG_TRUNC makes recv() report the full message size even when it does
    // not fit in `reply`, so an oversized reply is detected instead of being
    // parsed as if it were complete.
    reply_len = recv(fd, reply, sizeof(reply), MSG_TRUNC);
  } while (reply_len < 0 && errno == EINTR);

  if (std::cmp_less(reply_len, 0)) return std::nullopt;
  if (std::cmp_greater(reply_len, sizeof(reply))) return std::nullopt;

  constexpr int kKernelPortId = 0;
  int remaining = static_cast<int>(reply_len);
  for (nlmsghdr* nlh = reinterpret_cast<nlmsghdr*>(reply);
       nlh != nullptr && NLMSG_OK(nlh, remaining);
       nlh = NLMSG_NEXT(nlh, remaining)) {
    if (nlh->nlmsg_pid != kKernelPortId) continue;
    if (nlh->nlmsg_seq != req.nlh.nlmsg_seq) continue;
    if (nlh->nlmsg_type != RTM_NEWROUTE) continue;
    const rtmsg* rtm = static_cast<const rtmsg*>(NLMSG_DATA(nlh));
    int attr_len = static_cast<int>(RTM_PAYLOAD(nlh));
    for (const rtattr* rta = RTM_RTA(rtm);
         rta != nullptr && RTA_OK(rta, attr_len);
         rta = RTA_NEXT(rta, attr_len)) {
      if (rta->rta_type == RTA_OIF && sizeof(int) <= RTA_PAYLOAD(rta)) {
        int oif = -1;
        std::memcpy(&oif, RTA_DATA(rta), sizeof(oif));
        return oif;
      }
    }
  }
  return std::nullopt;
}
}  // namespace

/*static*/ std::optional<bool> Route::compute(const util::IpAddr& src,
                                              const util::IpAddr& dst) {
  if (src.AddressFamily() != dst.AddressFamily()) return false;
  if (src.IsLoopback()) return dst.IsLoopback();
  if (dst.IsLoopback()) return false;

  const absl::StatusOr<int> nic = FindNic(src);
  if (!nic.ok()) return std::nullopt;

  const std::optional<int> egress = FindEgressNic(src, dst);
  if (!egress.has_value()) return std::nullopt;

  return nic.value() == egress.value();
}

bool Route::Check(const util::IpAddr& src, const util::IpAddr& dst) {
  const Key key = {src, dst};
  {
    absl::MutexLock _(mu_);
    const auto it = routes_.find(key);
    if (it != routes_.end()) return it->second;
  }
  const std::optional<bool> routable = compute(src, dst);
  if (!routable.has_value()) return false;
  {
    absl::MutexLock _(mu_);  // First writer wins.
    const auto [it, inserted] = routes_.try_emplace(key, routable.value());
    return it->second;
  }
}

}  // namespace peregrine::internal
