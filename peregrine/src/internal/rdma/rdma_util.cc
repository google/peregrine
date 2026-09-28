#include "peregrine/src/internal/rdma/rdma_util.h"

#include <infiniband/verbs.h>

#include <cstring>
#include <optional>

#include "absl/log/check.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal {

std::optional<union ibv_gid> IpAddrToRdmaGid(const util::IpAddr& ip) {
  static_assert(assumptions::kUseIpAddrToRepresentRdmaRoCEv2Gid);
  if (ip.IsIPv4()) {
    return std::nullopt;
  } else {
    DCHECK(ip.IsIPv6());
    union ibv_gid gid;
    const util::ipv6_t& ip6 = ip.IPv6Addr();
    static_assert(sizeof(union ibv_gid) == sizeof(util::ipv6_t));
    std::memcpy(gid.raw, &ip6, sizeof(util::ipv6_t));
    return gid;
  }
}

}  // namespace peregrine::internal
