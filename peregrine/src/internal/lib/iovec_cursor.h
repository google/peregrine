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
      : nvecs_(iovs.size()),
        length_(TotalLength(iovs)),
        index_(0),
        bytes_left_(length_),
        iovs_(iovs.begin(), iovs.end()) {
    DCHECK(IsValid(iovs));
    DCHECK(invariant());
  }

  // Constructor with an initializer list.
  explicit IoVecCursor(std::initializer_list<const IoVec> iovs)
      : IoVecCursor(absl::MakeSpan(iovs)) {}

  // Returns the original total number of iovecs.
  size_t Size() const { return nvecs_; }

  // Returns the original total number of bytes.
  size_t Length() const { return length_; }

  // Returns the number of remaining iovecs.
  size_t Remaining() const { return nvecs_ - index_; }

  // Returns a const pointer to the current first iovec.
  const IoVec* Head() const {
    return index_ < nvecs_ ? &iovs_[index_] : nullptr;
  }

  // Advances the cursor by `bytes`, which must not go beyond the end.
  // This call can update some iovec but won't delete any.
  // Returns true iff all the iovecs are exhausted.
  bool Advance(size_t bytes) {
    DCHECK(invariant());

    DCHECK_LE(bytes, bytes_left_) << "out of range";
    bytes_left_ -= bytes;

    // Fast path for full iovec advances.
    if (bytes_left_ == 0) {
      index_ = nvecs_;
      return true;
    }

    // Slow path for partial iovec advances.
    while (index_ < nvecs_ && 0 < bytes) {
      IoVec& vec = iovs_[index_];
      if (const size_t len = vec.iov_len; len <= bytes) {
        // advance iovec index
        ++index_;
        bytes -= len;
      } else {
        // update current iovec ptr/len
        vec.iov_base = static_cast<char*>(vec.iov_base) + bytes;
        vec.iov_len -= bytes;
        break;
      }
    }
    DCHECK(invariant());
    DCHECK(index_ < nvecs_ || bytes == 0) << "out of range";
    return nvecs_ <= index_;
  }

  // Returns a string representation of the cursor.
  std::string ToString() const {
    return absl::StrFormat(
        "IoVecCursor(index/nvecs: %u/%u, bytes_left/length: %u/%u)", index_,
        nvecs_, bytes_left_, length_);
  }

 private:
  // Returns true if the cursor is in a valid state.
  bool invariant() const {
    return index_ <= nvecs_ && iovs_.size() == nvecs_ && IsValid(iovs_);
  }

 private:
  const size_t nvecs_;
  const size_t length_;
  size_t index_;
  size_t bytes_left_;
  absl::InlinedVector<IoVec, 4> iovs_;
};

inline std::ostream& operator<<(std::ostream& os, const IoVecCursor& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
