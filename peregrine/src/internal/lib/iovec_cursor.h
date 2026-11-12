#ifndef PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
#define PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
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

// This class provides a cursor interface for a sequence of `IoVec`s. All the
// provided iovecs must be valid: non-null base pointers and non-zero lengths.
// It is neither copyable nor movable. No resizing is performed on the
// underlying container during the lifetime of the cursor.
// Use this class as the interface (e.g., `IoVecCursor&` parameters). It does
// not own the iovec items and cannot be instantiated directly: instead use
// `IoVecCursorN<>` or `IoVecCursorV` to build concrete iovecs cursor objects.
// It is thread-compatible but not thread-safe.
class IoVecCursor {
 public:
  // Disallow copy and move.
  DISALLOW_COPY(IoVecCursor);
  DISALLOW_MOVE(IoVecCursor);

  // Returns true iff the remaining iovec items are valid.
  bool IsValid() const { return internal::IsValid(RemainingSpan()); }

  // Returns the original total number of iovec items.
  size_t TotalItems() const { return static_cast<size_t>(end_ - begin_); }

  // Returns the original total number of bytes spanned by the iovec items.
  size_t TotalBytes() const { return bytes_total_; }

  // Returns the number of remaining bytes to be advanced.
  size_t RemainingBytes() const { return bytes_left_; }

  // Returns the number of remaining iovec items to be advanced.
  size_t RemainingItems() const { return static_cast<size_t>(end_ - cur_); }

  // Returns a const pointer to the current first iovec.
  const IoVec* Head() const { return cur_ < end_ ? cur_ : nullptr; }

  // Returns a pointer to the current first iovec.
  IoVec* Head() { return const_cast<IoVec*>(std::as_const(*this).Head()); }

  // Returns a constant span of the remaining iovec items.
  absl::Span<const IoVec> RemainingSpan() const {
    DCHECK_LE(cur_, end_);
    return absl::MakeConstSpan(cur_, end_);
  }

  // Returns a mutable span of the remaining iovec items.
  absl::Span<IoVec> RemainingSpan() {
    DCHECK_LE(cur_, end_);
    return absl::MakeSpan(cur_, end_);
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
    return !(cur < end);
  }

  // Returns a string representation of the cursor.
  std::string ToString() const {
    return absl::StrFormat(
        "IoVecCursor(index/size: %v/%v, bytes_left/total: %v/%v)",
        cur_ - begin_, TotalItems(), bytes_left_, bytes_total_);
  }

 protected:
  // Constructor.
  IoVecCursor() = default;

  // Destructor.
  ~IoVecCursor() = default;

  // Points this cursor at the input span in [1, 2^63 - 1] bytes. The iovec
  // items are owned by the subclass, are updated in place by `Advance()`,
  // and must outlive this cursor. Must be called by a subclass constructor.
  void init(absl::Span<IoVec> iovs) {
    begin_ = iovs.data();
    cur_ = begin_;
    end_ = begin_ + iovs.size();
    bytes_left_ = TotalLength(iovs);
    bytes_total_ = bytes_left_;
    DCHECK_GE(TotalLength(iovs), 1);
    DCHECK_LT(TotalLength(iovs), 1ULL << 63);
    DCHECK(invariant());
  }

 private:
  // Returns true if the cursor is in a valid state.
  bool invariant() const {
    return internal::IsValid(absl::MakeConstSpan(begin_, end_)) &&
           begin_ <= cur_ && cur_ <= end_ && bytes_left_ <= bytes_total_ &&
           bytes_left_ == TotalLength(absl::MakeConstSpan(cur_, end_)) &&
           bytes_left_ <= std::numeric_limits<ssize_t>::max();
  }

 private:
  IoVec* begin_;
  IoVec* cur_;
  IoVec* end_;
  size_t bytes_left_;
  size_t bytes_total_;
};

// This class provides a cursor for a fixed-size sequence of `IoVec`s.
// It is a subclass of `IoVecCursor` to avoid dynamic allocation of the
// underlying container.
template <int kN>
class IoVecCursorN final : public IoVecCursor {
  static_assert(kN >= 1, "at least one iovec is required");

 public:
  // Constructor with an array of `IoVec`s (in [1, 2^63 - 1] bytes).
  explicit IoVecCursorN(const std::array<IoVec, kN>& iovs) : iovs_(iovs) {
    init(absl::MakeSpan(iovs_));
  }

  // Constructor with a sequence of `IoVec`s (in [1, 2^63 - 1] bytes).
  template <int kM>
  explicit IoVecCursorN(const IoVec (&iovs)[kM]) {
    static_assert(kM == kN, "wrong number of iovecs");
    std::copy_n(iovs, kM, iovs_.begin());
    init(absl::MakeSpan(iovs_));
  }

 private:
  std::array<IoVec, kN> iovs_;
};

// This class provides a cursor for a variable-size sequence of `IoVec`s.
// It is a subclass of `IoVecCursor` to allow dynamic allocation of the
// underlying container.
class IoVecCursorV final : public IoVecCursor {
 public:
  // Constructor with a span of `IoVec`s (in [1, 2^63 - 1] bytes).
  explicit IoVecCursorV(absl::Span<const IoVec> iovs)
      : iovs_(iovs.begin(), iovs.end()) {
    init(absl::MakeSpan(iovs_));
  }

 private:
  absl::InlinedVector<IoVec, 4> iovs_;
};

inline std::ostream& operator<<(std::ostream& os, const IoVecCursor& c) {
  return os << c.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_IOVEC_CURSOR_H_
