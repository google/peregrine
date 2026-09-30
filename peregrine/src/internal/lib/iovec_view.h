#ifndef PEREGRINE_SRC_INTERNAL_LIB_IOVEC_VIEW_H_
#define PEREGRINE_SRC_INTERNAL_LIB_IOVEC_VIEW_H_

#include <cstddef>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal {

// This class provides a view of an array of `IoVec`s.
// It is thread-compatible but not thread-safe.
class IoVecView {
 public:
  // Constructor.
  explicit IoVecView(absl::Span<const IoVec> iovs)
      : size_(iovs.size()), index_(0), vecs_(iovs.begin(), iovs.end()) {}

  // Returns the total number of iovec's.
  size_t size() const {
    DCHECK_EQ(vecs_.size(), size_);
    return size_;
  }

  // Returns a pointer to the `i`-th iovec.
  IoVec* operator[](size_t i) {
    DCHECK_LT(i, size_);
    return &vecs_[i];
  }

  // Returns a const pointer to the `i`-th iovec.
  const IoVec* operator[](size_t i) const {
    DCHECK_LT(i, size_);
    return &vecs_[i];
  }

  // Advances the view by `bytes`.
  // It can update some io iovec's but won't delete any.
  // Returns the new index of the first iovec in the view.
  size_t Advance(size_t bytes) {
    while (index_ < size_ && 0 < bytes) {  // advance index
      IoVec& vec = vecs_[index_];
      if (const size_t len = vec.iov_len; len <= bytes) {
        ++index_;
        bytes -= len;
      } else {
        vec.iov_base = static_cast<char*>(vec.iov_base) + bytes;
        vec.iov_len -= bytes;
        break;
      }
    }
    return index_;
  }

 private:
  const size_t size_;
  size_t index_;
  std::vector<IoVec> vecs_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_IOVEC_VIEW_H_
