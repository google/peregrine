#include "peregrine/src/internal/rdma/rdma_context.h"

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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {
namespace {

using util::Errno;

constexpr int kDefaultCqeSize = 1024;

std::string ErrMsg(const std::string_view prefix, const Errno err) {
  return absl::StrFormat("%s: errno=%d (%s)", prefix, err.value(),
                         std::strerror(err.value()));
}

std::string_view getDeviceName(struct ibv_device* dev) {
  if (dev == nullptr) return "unknown";
  const char* name = ibv_get_device_name(dev);
  return name != nullptr ? name : "unknown";
}

std::string_view getDeviceName(struct ibv_context* ctx) {
  return ctx != nullptr ? getDeviceName(ctx->device) : "unknown";
}

struct ibv_context* openDevice(struct ibv_device* dev) {
  struct ibv_context* ctx = ibv_open_device(dev);
  if (ctx == nullptr) {
    const Errno err(errno);
    const std::string s =
        absl::StrCat("ibv_open_device failed for ", getDeviceName(dev));
    LOG(WARNING) << ErrMsg(s, err);
  }
  return ctx;
}

void queryDevice(struct ibv_context* ctx, struct ibv_device_attr& attr) {
  std::memset(&attr, 0, sizeof(attr));
  if (ibv_query_device(ctx, &attr) != 0) {
    const Errno err(errno);
    const std::string s =
        absl::StrCat("ibv_query_device failed for ", getDeviceName(ctx));
    LOG(WARNING) << ErrMsg(s, err);
  }
}

void checkPortStates(struct ibv_context* ctx,
                     const struct ibv_device_attr& attr) {
  int active_ports = 0;
  for (auto port = 1; port <= attr.phys_port_cnt; ++port) {
    struct ibv_port_attr port_attr;
    if (ibv_query_port(ctx, port, &port_attr) == 0) {
      if (port_attr.state == IBV_PORT_ACTIVE) ++active_ports;
    }
  }
  if (active_ports == 0 && attr.phys_port_cnt > 0) {
    LOG(WARNING) << absl::StrFormat(
        "RDMA device %s opened, but all %d physical ports report "
        "DOWN or inactive link status",
        getDeviceName(ctx), attr.phys_port_cnt);
  }
}

struct ibv_pd* allocPd(struct ibv_context* ctx) {
  struct ibv_pd* pd = ibv_alloc_pd(ctx);
  if (pd == nullptr) {
    const Errno err(errno);
    const std::string s =
        absl::StrCat("ibv_alloc_pd failed for ", getDeviceName(ctx));
    LOG(WARNING) << ErrMsg(s, err);
  }
  return pd;
}

struct ibv_cq* createCq(struct ibv_context* ctx,
                        const struct ibv_device_attr& attr) {
  const int cqe = attr.max_cqe > 0 ? attr.max_cqe : kDefaultCqeSize;
  // TODO: transition to multiple CQs per device (one per polling worker
  // thread) to enable zero-contention lock-free polling across multiple QPs.
  struct ibv_cq* cq = ibv_create_cq(ctx, cqe, /*cq_context=*/nullptr,
                                    /*channel=*/nullptr, /*comp_vector=*/0);
  if (cq == nullptr) {
    const Errno err(errno);
    const std::string s =
        absl::StrCat("ibv_create_cq failed for ", getDeviceName(ctx));
    LOG(WARNING) << ErrMsg(s, err);
  }
  return cq;
}

int findRoutableGid(struct ibv_context* ctx, uint8_t port_num) {
  if (ctx == nullptr) return 0;
  struct ibv_port_attr port_attr = {};
  if (ibv_query_port(ctx, port_num, &port_attr) != 0) return 0;

  int fallback_ipv6_idx = -1;
  for (int i = 0; i < port_attr.gid_tbl_len; ++i) {
    struct ibv_gid_entry entry = {};
    if (ibv_query_gid_ex(ctx, port_num, i, &entry, 0) == 0) {
      if (entry.gid_type != IBV_GID_TYPE_ROCE_V2) continue;

      const auto* in6 = reinterpret_cast<const struct in6_addr*>(entry.gid.raw);
      // 1. Highest priority: RoCEv2 IPv4-mapped address (::ffff:A.B.C.D)
      if (IN6_IS_ADDR_V4MAPPED(in6)) return i;

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

RdmaContext::RdmaContext(struct ibv_context* ctx, struct ibv_pd* pd,
                         struct ibv_cq* cq, int gid_index,
                         const union ibv_gid& local_gid,
                         const struct ibv_device_attr& attr)
    : name_(getDeviceName(ctx)),
      ctx_(ctx),
      pd_(pd),
      cq_(cq),
      gid_index_(gid_index),
      local_gid_(local_gid),
      attr_(attr) {
  DCHECK_NE(ctx_, nullptr);
  DCHECK_NE(pd_, nullptr);
  DCHECK_NE(cq_, nullptr);
  LOG(INFO) << "RDMA device context created for: " << name_
            << " (max_cqe=" << attr_.max_cqe
            << ", ports=" << static_cast<int>(attr_.phys_port_cnt)
            << ", gid_index=" << gid_index_ << ")";
}

RdmaContext::~RdmaContext() {
  if (cq_ != nullptr) {
    if (ibv_destroy_cq(cq_) != 0) {
      const Errno err(errno);
      LOG(WARNING) << ErrMsg(
          absl::StrFormat("failed to destroy CQ on device %s", name_), err);
    }
  }
  if (pd_ != nullptr) {
    if (ibv_dealloc_pd(pd_) != 0) {
      const Errno err(errno);
      LOG(WARNING) << ErrMsg(
          absl::StrFormat("failed to dealloc PD on device %s", name_), err);
    }
  }
  if (ctx_ != nullptr) {
    if (ibv_close_device(ctx_) != 0) {
      const Errno err(errno);
      LOG(WARNING) << ErrMsg(
          absl::StrFormat("failed to close device %s", name_), err);
    }
  }
  LOG(INFO) << "RDMA device context destroyed for: " << name_;
}

std::unique_ptr<RdmaContext> RdmaContext::Create(struct ibv_device* device) {
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
    const Errno err(errno);
    LOG(WARNING) << ErrMsg(
        absl::StrFormat("ibv_query_gid failed for %s on port %d, gid_index %d",
                        getDeviceName(context), kDefaultPort, gid_index),
        err);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
    return nullptr;
  }

  return absl::WrapUnique(
      new RdmaContext(context, pd, cq, gid_index, local_gid, attr));
}

}  // namespace peregrine::internal
