#ifndef PEREGRINE_SRC_INTERNAL_RDMA_IBV_LOADER_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_IBV_LOADER_H_

// libibverbs entry points; real calls, or ENOSYS stubs under PEREGRINE_NO_RDMA.

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"

namespace peregrine::internal::ibv {

#ifdef PEREGRINE_NO_RDMA

// Built without RDMA (OSS default): libibverbs is not linked.
inline absl::Status EnsureLoaded() {
  return absl::UnavailableError(
      "RDMA transport unavailable: Peregrine was built without RDMA support "
      "(libibverbs not linked). Rebuild with --define=peregrine_rdma=true or "
      "select TransportType::kTcp.");
}
inline bool IsAvailable() { return false; }

// Stubs mimic libibverbs failure conventions: nullptr / ENOSYS, errno = ENOSYS.
inline int Fail() {
  errno = ENOSYS;
  return ENOSYS;
}
template <typename T>
inline T* FailPtr() {
  errno = ENOSYS;
  return nullptr;
}

inline struct ibv_device** GetDeviceList(int* n) {
  if (n != nullptr) *n = 0;
  return FailPtr<struct ibv_device*>();
}
inline void FreeDeviceList(struct ibv_device**) {}
inline const char* GetDeviceName(struct ibv_device*) {
  return FailPtr<const char>();
}
inline struct ibv_context* OpenDevice(struct ibv_device*) {
  return FailPtr<struct ibv_context>();
}
inline int CloseDevice(struct ibv_context*) { return Fail(); }
inline int QueryDevice(struct ibv_context*, struct ibv_device_attr*) {
  return Fail();
}
inline int QueryPort(struct ibv_context*, uint8_t, struct ibv_port_attr*) {
  return Fail();
}
inline int QueryGid(struct ibv_context*, uint8_t, int, union ibv_gid*) {
  return Fail();
}
inline int QueryGidEx(struct ibv_context*, uint32_t, uint32_t,
                      struct ibv_gid_entry*, uint32_t) {
  return Fail();
}
inline struct ibv_pd* AllocPd(struct ibv_context*) {
  return FailPtr<struct ibv_pd>();
}
inline int DeallocPd(struct ibv_pd*) { return Fail(); }
inline struct ibv_mr* RegMr(struct ibv_pd*, void*, size_t, int) {
  return FailPtr<struct ibv_mr>();
}
inline int DeregMr(struct ibv_mr*) { return Fail(); }
inline struct ibv_cq* CreateCq(struct ibv_context*, int, void*,
                               struct ibv_comp_channel*, int) {
  return FailPtr<struct ibv_cq>();
}
inline int DestroyCq(struct ibv_cq*) { return Fail(); }
inline struct ibv_qp* CreateQp(struct ibv_pd*, struct ibv_qp_init_attr*) {
  return FailPtr<struct ibv_qp>();
}
inline int DestroyQp(struct ibv_qp*) { return Fail(); }
inline int ModifyQp(struct ibv_qp*, struct ibv_qp_attr*, int) { return Fail(); }
inline const char* WcStatusStr(enum ibv_wc_status) {
  return "RDMA support not compiled in";
}

#else  // PEREGRINE_NO_RDMA

// libibverbs is linked (google3 static, or OSS with --config=rdma).
inline absl::Status EnsureLoaded() { return absl::OkStatus(); }
inline bool IsAvailable() { return true; }

inline struct ibv_device** GetDeviceList(int* n) {
  return ::ibv_get_device_list(n);
}
inline void FreeDeviceList(struct ibv_device** l) { ::ibv_free_device_list(l); }
inline const char* GetDeviceName(struct ibv_device* d) {
  return ::ibv_get_device_name(d);
}
inline struct ibv_context* OpenDevice(struct ibv_device* d) {
  return ::ibv_open_device(d);
}
inline int CloseDevice(struct ibv_context* c) { return ::ibv_close_device(c); }
inline int QueryDevice(struct ibv_context* c, struct ibv_device_attr* a) {
  return ::ibv_query_device(c, a);
}
// <verbs.h> macro; expands to the inline ___ibv_query_port.
inline int QueryPort(struct ibv_context* c, uint8_t p,
                     struct ibv_port_attr* a) {
  return ibv_query_port(c, p, a);
}
inline int QueryGid(struct ibv_context* c, uint8_t p, int i, union ibv_gid* g) {
  return ::ibv_query_gid(c, p, i, g);
}
inline int QueryGidEx(struct ibv_context* c, uint32_t p, uint32_t i,
                      struct ibv_gid_entry* e, uint32_t f) {
  return ::ibv_query_gid_ex(c, p, i, e, f);
}
inline struct ibv_pd* AllocPd(struct ibv_context* c) {
  return ::ibv_alloc_pd(c);
}
inline int DeallocPd(struct ibv_pd* pd) { return ::ibv_dealloc_pd(pd); }
// Function in the google3 header, macro upstream; unqualified fits both.
inline struct ibv_mr* RegMr(struct ibv_pd* pd, void* addr, size_t len,
                            int access) {
  return ibv_reg_mr(pd, addr, len, access);
}
inline int DeregMr(struct ibv_mr* mr) { return ::ibv_dereg_mr(mr); }
inline struct ibv_cq* CreateCq(struct ibv_context* c, int cqe, void* ctx,
                               struct ibv_comp_channel* ch, int vec) {
  return ::ibv_create_cq(c, cqe, ctx, ch, vec);
}
inline int DestroyCq(struct ibv_cq* cq) { return ::ibv_destroy_cq(cq); }
inline struct ibv_qp* CreateQp(struct ibv_pd* pd, struct ibv_qp_init_attr* a) {
  return ::ibv_create_qp(pd, a);
}
inline int DestroyQp(struct ibv_qp* qp) { return ::ibv_destroy_qp(qp); }
inline int ModifyQp(struct ibv_qp* qp, struct ibv_qp_attr* a, int mask) {
  return ::ibv_modify_qp(qp, a, mask);
}
inline const char* WcStatusStr(enum ibv_wc_status s) {
  return ::ibv_wc_status_str(s);
}

#endif  // PEREGRINE_NO_RDMA

}  // namespace peregrine::internal::ibv

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_IBV_LOADER_H_
