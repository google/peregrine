#ifndef PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
#define PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_

#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <string>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/util/util.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class provides a cursor for a sequence of `IoVec`s. All the provided
// iovecs must be valid: non-null base pointers and non-zero lengths.
// It is neither copyable nor movable. No resizing is performed on the
// underlying container during the lifetime of the cursor.
// It is thread-compatible but not thread-safe.
class IoVecCursor {
 public:
  // Constructor.
  explicit IoVecCursor(absl::Span<const IoVec> iovs)
      : iovs_(iovs.begin(), iovs.end()),
        cur_(iovs_.data()),
        end_(cur_ + iovs_.size()),
        bytes_left_(TotalLength(iovs)),
        length_(bytes_left_) {
    DCHECK(IsValid(iovs));
    DCHECK(invariant());
  }

  // Constructor with an initializer list.
  explicit IoVecCursor(std::initializer_list<const IoVec> iovs)
      : IoVecCursor(absl::Span<const IoVec>(iovs.begin(), iovs.size())) {}

  // Disallow copy and move.
  DISALLOW_COPY(IoVecCursor);
  DISALLOW_MOVE(IoVecCursor);

  // Destructor.
  ~IoVecCursor() = default;

  // Returns the original total number of iovecs.
  size_t Size() const { return iovs_.size(); }

  // Returns the original total number of bytes.
  size_t Length() const { return length_; }

  // Returns the number of remaining iovecs.
  size_t Remaining() const { return static_cast<size_t>(end_ - cur_); }

  // Returns a const pointer to the current first iovec.
  const IoVec* Head() const { return cur_ < end_ ? cur_ : nullptr; }

  // Returns a pointer to the current first iovec.
  IoVec* Head() { return const_cast<IoVec*>(std::as_const(*this).Head()); }

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
      cur_ = end_;
      return true;
    }

    // Slow path for partial iovec advances.
    IoVec* cur = cur_;
    IoVec* const end = end_;
    while (cur < end) {
      if (const size_t len = cur->iov_len; bytes < len) {
        cur->iov_base = static_cast<char*>(cur->iov_base) + bytes;
        cur->iov_len = len - bytes;
        break;
      } else {
        ++cur;
        bytes -= len;
      }
    }
    cur_ = cur;
    DCHECK(invariant());
    DCHECK(cur < end) << "out of range";
    return end <= cur;
  }

  // Returns a string representation of the cursor.
  std::string ToString() const {
    return absl::StrFormat(
        "IoVecCursor(index/size: %v/%v, bytes_left/length: %v/%v)",
        cur_ - iovs_.data(), iovs_.size(), bytes_left_, length_);
  }

 private:
  // Returns true if the cursor is in a valid state.
  bool invariant() const {
    return iovs_.data() <= cur_ && cur_ <= end_ && bytes_left_ <= length_ &&
           end_ == iovs_.data() + iovs_.size() && IsValid(iovs_) &&
           bytes_left_ == TotalLength(absl::MakeConstSpan(cur_, end_));
  }

 private:
  absl::InlinedVector<IoVec, 4> iovs_;
  IoVec* cur_;
  IoVec* const end_;
  size_t bytes_left_;
  const size_t length_;
};

inline std::ostream& operator<<(std::ostream& os, const IoVecCursor& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
