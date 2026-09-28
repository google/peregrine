#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_UTIL_H_

#include <infiniband/verbs.h>

#include <optional>

#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal {

// Returns the RDMA gid for the ipv6 address, or std::nullopt for ipv4.
std::optional<union ibv_gid> IpAddrToRdmaGid(const util::IpAddr& ip);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_UTIL_H_
