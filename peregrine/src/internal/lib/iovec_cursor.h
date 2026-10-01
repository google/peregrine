#ifndef PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
#define PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_

#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <string>

#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine::internal {

// This class provides a cursor for a sequence of `IoVec`s. All the provided
// iovecs must be valid: non-null base pointers and non-zero lengths.
// No resizing is performed on the underlying container.
// It is thread-compatible but not thread-safe.
class IoVecCursor {
 public:
  // Constructor.
  explicit IoVecCursor(absl::Span<const IoVec> iovs)
      : index_(0),
        size_(iovs.size()),
        bytes_left_(TotalLength(iovs)),
        length_(bytes_left_),
        iovs_(iovs.begin(), iovs.end()) {
    DCHECK(IsValid(iovs));
    DCHECK(invariant());
  }

  // Constructor with an initializer list.
  explicit IoVecCursor(std::initializer_list<const IoVec> iovs)
      : IoVecCursor(absl::Span<const IoVec>(iovs.begin(), iovs.size())) {}

  // Returns the original total number of iovec items.
  size_t Size() const { return size_; }

  // Returns the original total number of bytes.
  size_t Length() const { return length_; }

  // Returns the number of remaining iovecs.
  size_t Remaining() const {
    DCHECK_LE(index_, size_);
    return size_ - index_;
  }

  // Returns a const pointer to the current first iovec.
  const IoVec* Head() const {
    return index_ < size_ ? iovs_.data() + index_ : nullptr;
  }

  // Advances the cursor by `bytes`, which must not go beyond the end.
  // This call can update some iovec items but won't delete any.
  // Returns true iff all the iovec items are exhausted.
  bool Advance(size_t bytes) {
    DCHECK_GE(bytes, 1) << "zero byte";
    DCHECK_LE(bytes, bytes_left_) << "out of range";
    DCHECK(invariant());

    // Fast path for full iovec advances.
    bytes_left_ -= bytes;
    if (bytes_left_ == 0) {
      index_ = size_;
      return true;
    }

    // Slow path for partial iovec advances.
    const auto size = size_;
    auto index = index_;
    while (index < size) {
      IoVec& vec = iovs_[index];
      if (const size_t len = vec.iov_len; bytes < len) {
        vec.iov_base = static_cast<char*>(vec.iov_base) + bytes;
        vec.iov_len = len - bytes;
        break;
      } else {
        ++index;
        bytes -= len;
      }
    }
    index_ = index;
    DCHECK(invariant());
    DCHECK(index < size || bytes == 0) << "out of range";
    return size <= index;
  }

  // Returns a string representation of the cursor.
  std::string ToString() const {
    return absl::StrFormat(
        "IoVecCursor(index/nvecs: %u/%u, bytes_left/length: %u/%u)", index_,
        size_, bytes_left_, length_);
  }

 private:
  // Returns true if the cursor is in a valid state.
  bool invariant() const {
    return index_ <= size_ && iovs_.size() == size_ && IsValid(iovs_);
  }

 private:
  size_t index_;
  const size_t size_;
  size_t bytes_left_;
  const size_t length_;
  absl::InlinedVector<IoVec, 4> iovs_;
};

inline std::ostream& operator<<(std::ostream& os, const IoVecCursor& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
