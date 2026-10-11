#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "peregrine/src/internal/rdma/rdma_qpair.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class represents an established RDMA connection. When implementing
// this class, please follow the pattern in `socket/tcp_socket.h` to simplify
// the implementation of the `RdmaChannel`.
// It is thread-compatible but not thread-safe.
class RdmaConn final {
 public:
  // Constructor.
  RdmaConn(std::unique_ptr<RdmaQPair> qp, uint32_t lkey, uint32_t rkey)
      : qp_(std::move(qp)), lkey_(lkey), rkey_(rkey) {
    DCHECK_NE(qp_, nullptr);
  }

  // Disallows copy and move.
  DISALLOW_COPY(RdmaConn);
  DISALLOW_MOVE(RdmaConn);

  // Destructor.
  ~RdmaConn() = default;

  // Returns the underlying Queue Pair.
  RdmaQPair* Qp() const { return qp_.get(); }

  // Returns the local LKey.
  uint32_t LKey() const { return lkey_; }

  // Returns the remote RKey.
  uint32_t RKey() const { return rkey_; }

 private:
  std::unique_ptr<RdmaQPair> qp_;
  uint32_t lkey_;
  uint32_t rkey_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_CONN_H_
