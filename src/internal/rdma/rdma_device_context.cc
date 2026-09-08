#include "src/internal/rdma/rdma_device_context.h"

#include <infiniband/verbs.h>
#include <netinet/in.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"

namespace peregrine::internal {
namespace {

constexpr int kDefaultCqeSize = 1024;

std::string ErrorMsg(std::string_view prefix, int last_errno) {
  return absl::StrFormat("%s: errno=%d (%s)", prefix, last_errno,
                         std::strerror(last_errno));
}

std::string_view getDeviceName(struct ibv_device* device) {
  if (device == nullptr) return "unknown";
  const char* name = ibv_get_device_name(device);
  return name != nullptr ? name : "unknown";
}

std::string_view getDeviceName(struct ibv_context* context) {
  return context != nullptr ? getDeviceName(context->device) : "unknown";
}

struct ibv_context* openDevice(struct ibv_device* device) {
  struct ibv_context* context = ibv_open_device(device);
  if (context == nullptr) {
    LOG(WARNING) << ErrorMsg(
        absl::StrFormat("ibv_open_device failed for %s", getDeviceName(device)),
        errno);
  }
  return context;
}

void queryDevice(struct ibv_context* context, struct ibv_device_attr& attr) {
  std::memset(&attr, 0, sizeof(attr));
  if (ibv_query_device(context, &attr) != 0) {
    LOG(WARNING) << ErrorMsg(
        absl::StrFormat("ibv_query_device failed for %s (using defaults)",
                        getDeviceName(context)),
        errno);
  }
}

void checkPortStates(struct ibv_context* context,
                     const struct ibv_device_attr& attr) {
  int active_ports = 0;
  for (uint8_t port = 1; port <= attr.phys_port_cnt; ++port) {
    struct ibv_port_attr port_attr;
    if (ibv_query_port(context, port, &port_attr) == 0) {
      if (port_attr.state == IBV_PORT_ACTIVE) {
        active_ports++;
      }
    }
  }
  if (active_ports == 0 && attr.phys_port_cnt > 0) {
    LOG(WARNING) << absl::StrFormat(
        "RDMA device %s opened, but all %d physical ports report DOWN or "
        "inactive link status",
        getDeviceName(context), attr.phys_port_cnt);
  }
}

struct ibv_pd* allocPd(struct ibv_context* context) {
  struct ibv_pd* pd = ibv_alloc_pd(context);
  if (pd == nullptr) {
    LOG(WARNING) << ErrorMsg(
        absl::StrFormat("ibv_alloc_pd failed for %s", getDeviceName(context)),
        errno);
  }
  return pd;
}

struct ibv_cq* createCq(struct ibv_context* context,
                        const struct ibv_device_attr& attr) {
  const int cqe = attr.max_cqe > 0 ? attr.max_cqe : kDefaultCqeSize;
  // TODO: transition to multiple CQs per device (one per polling worker
  // thread) to enable zero-contention lock-free polling across multiple QPs.
  struct ibv_cq* cq = ibv_create_cq(context, cqe, /*cq_context=*/nullptr,
                                    /*channel=*/nullptr, /*comp_vector=*/0);
  if (cq == nullptr) {
    LOG(WARNING) << ErrorMsg(
        absl::StrFormat("ibv_create_cq failed for %s", getDeviceName(context)),
        errno);
  }
  return cq;
}

int findRoutableGid(struct ibv_context* context, uint8_t port_num) {
  if (context == nullptr) return 0;
  struct ibv_port_attr port_attr = {};
  if (ibv_query_port(context, port_num, &port_attr) != 0) {
    return 0;
  }

  int fallback_ipv6_idx = -1;

  for (int i = 0; i < port_attr.gid_tbl_len; ++i) {
    struct ibv_gid_entry entry = {};
    if (ibv_query_gid_ex(context, port_num, i, &entry, 0) == 0) {
      if (entry.gid_type != IBV_GID_TYPE_ROCE_V2) continue;

      const auto* in6 = reinterpret_cast<const struct in6_addr*>(entry.gid.raw);

      // 1. Highest priority: RoCEv2 IPv4-mapped address (::ffff:A.B.C.D)
      if (IN6_IS_ADDR_V4MAPPED(in6)) {
        return i;
      }

      // 2. Secondary priority: Global Routable IPv6 (not link-local, loopback,
      // or multicast)
      if (!IN6_IS_ADDR_LINKLOCAL(in6) && !IN6_IS_ADDR_LOOPBACK(in6) &&
          !IN6_IS_ADDR_MULTICAST(in6) && fallback_ipv6_idx == -1) {
        fallback_ipv6_idx = i;
      }
    }
  }

  return fallback_ipv6_idx != -1 ? fallback_ipv6_idx : 0;
}

}  // namespace

RdmaDeviceContext::RdmaDeviceContext(struct ibv_context* context,
                                     struct ibv_pd* pd, struct ibv_cq* cq,
                                     const struct ibv_device_attr& device_attr,
                                     int gid_index,
                                     const union ibv_gid& local_gid)
    : name_(getDeviceName(context)),
      context_(context),
      pd_(pd),
      cq_(cq),
      device_attr_(device_attr),
      gid_index_(gid_index),
      local_gid_(local_gid) {
  DCHECK_NE(context_, nullptr);
  DCHECK_NE(pd_, nullptr);
  DCHECK_NE(cq_, nullptr);
  LOG(INFO) << "RDMA device context created for: " << name_
            << " (max_cqe=" << device_attr_.max_cqe
            << ", ports=" << static_cast<int>(device_attr_.phys_port_cnt)
            << ", gid_index=" << gid_index_ << ")";
}

RdmaDeviceContext::~RdmaDeviceContext() {
  if (cq_ != nullptr) {
    if (ibv_destroy_cq(cq_) != 0) {
      LOG(WARNING) << ErrorMsg(
          absl::StrFormat("failed to destroy CQ on device %s", name_), errno);
    }
  }
  if (pd_ != nullptr) {
    if (ibv_dealloc_pd(pd_) != 0) {
      LOG(WARNING) << ErrorMsg(
          absl::StrFormat("failed to dealloc PD on device %s", name_), errno);
    }
  }
  if (context_ != nullptr) {
    if (ibv_close_device(context_) != 0) {
      LOG(WARNING) << ErrorMsg(
          absl::StrFormat("failed to close device %s", name_), errno);
    }
  }
  LOG(INFO) << "RDMA device context destroyed for: " << name_;
}

std::unique_ptr<RdmaDeviceContext> RdmaDeviceContext::Create(
    struct ibv_device* device) {
  DCHECK_NE(device, nullptr);
  struct ibv_context* context = openDevice(device);
  if (context == nullptr) {
    return nullptr;
  }

  struct ibv_device_attr attr;
  queryDevice(context, attr);
  checkPortStates(context, attr);

  struct ibv_pd* pd = allocPd(context);
  if (pd == nullptr) {
    ibv_close_device(context);
    return nullptr;
  }

  struct ibv_cq* cq = createCq(context, attr);
  if (cq == nullptr) {
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    return nullptr;
  }

  const int gid_index = findRoutableGid(context, kDefaultPort);
  union ibv_gid local_gid = {};
  if (ibv_query_gid(context, kDefaultPort, gid_index, &local_gid) != 0) {
    LOG(WARNING) << ErrorMsg(
        absl::StrFormat("ibv_query_gid failed for %s on port %d, gid_index %d",
                        getDeviceName(context), kDefaultPort, gid_index),
        errno);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    return nullptr;
  }

  return absl::WrapUnique(
      new RdmaDeviceContext(context, pd, cq, attr, gid_index, local_gid));
}

}  // namespace peregrine::internal
