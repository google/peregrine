#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_

#include <cstdint>
#include <memory>

#include "peregrine/src/internal/rdma/rdma_qpair.h"

namespace peregrine::internal {

// This struct represents an established RDMA connection.
// It is thread-compatible but not thread-safe.
struct RdmaConn final {
  std::unique_ptr<RdmaQPair> qp;
  uint32_t lkey;
  uint32_t rkey;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_
