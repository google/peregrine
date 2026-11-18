#ifndef PEREGRINE_SRC_INTERNAL_LIB_SPSC_QUEUE_H_
#define PEREGRINE_SRC_INTERNAL_LIB_SPSC_QUEUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/optimization.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// A fixed-capacity, lock-free single-producer/single-consumer (SPSC) FIFO
// queue. Capacity is rounded up to a power of two; `TryEnqueue` returns false
// without blocking when the queue is full.
//
// Thread-safety: One producer (`IsFull`, `TryEnqueue`) and one consumer
// (`IsEmpty`, `Dequeue`) may operate concurrently without locks.
template <typename T>
class BoundedSpscQueue final {
 public:
  static constexpr size_t kDefaultCapacity = 1024;

  // Creates a queue with capacity rounded up to a power of two >=
  // `min_capacity` (clamped to at least 1). `T` need not be
  // default-constructible.
  explicit BoundedSpscQueue(size_t min_capacity = kDefaultCapacity)
      : capacity_(std::bit_ceil(min_capacity == 0 ? size_t{1} : min_capacity)),
        mask_(capacity_ - 1),
        slots_(std::make_unique<std::optional<T>[]>(capacity_)) {}

  DISALLOW_COPY(BoundedSpscQueue);
  DISALLOW_MOVE(BoundedSpscQueue);

  ~BoundedSpscQueue() = default;

  // Returns the usable slot capacity (always a power of two).
  size_t Capacity() const noexcept { return capacity_; }

  // Returns an approximate number of queued elements.
  size_t SizeApprox() const noexcept {
    const size_t w = acquireWriteIdx();
    const size_t r = acquireReadIdx();
    return w >= r ? (w - r) : 0;
  }

  // Producer-only: returns true iff no free slot is available.
  bool IsFull() const noexcept {
    const size_t w = getWriteIdx();
    if (ABSL_PREDICT_TRUE(w - read_idx_cache_ < capacity_)) return false;
    read_idx_cache_ = acquireReadIdx();
    return w - read_idx_cache_ >= capacity_;
  }

  // Consumer-only: returns true iff the queue is currently empty.
  bool IsEmpty() const noexcept {
    const size_t r = getReadIdx();
    if (ABSL_PREDICT_TRUE(r != write_idx_cache_)) return false;
    write_idx_cache_ = acquireWriteIdx();
    return r == write_idx_cache_;
  }

  // Producer-only: copies `value` into the queue if space is available.
  bool TryEnqueue(const T& value) { return tryEmplace(value); }

  // Producer-only: moves `value` into the queue if space is available.
  bool TryEnqueue(T&& value) { return tryEmplace(std::move(value)); }

  // Consumer-only: moves the oldest element into `out` and destroys its slot.
  // Returns false if the queue is empty.
  bool Dequeue(T& out) {
    if (ABSL_PREDICT_FALSE(IsEmpty())) return false;
    const size_t r = getReadIdx();
    out = *std::move(slots_[r & mask_]);
    slots_[r & mask_].reset();
    publishReadIdx(r + 1);
    return true;
  }

 private:
  // Producer-only: stores `value` if space is available. Returns false (without
  // consuming `value`) when full.
  template <typename U>
  bool tryEmplace(U&& value) {
    if (ABSL_PREDICT_FALSE(IsFull())) return false;
    const size_t w = getWriteIdx();
    slots_[w & mask_].emplace(std::forward<U>(value));
    publishWriteIdx(w + 1);
    return true;
  }

  // Owner-thread relaxed index reads (`write_idx_` is written only by the
  // producer; `read_idx_` is written only by the consumer) and cross-thread
  // acquire/release index synchronization helpers.
  size_t getWriteIdx() const noexcept {
    return write_idx_.load(std::memory_order_relaxed);
  }
  size_t getReadIdx() const noexcept {
    return read_idx_.load(std::memory_order_relaxed);
  }
  size_t acquireWriteIdx() const noexcept {
    return write_idx_.load(std::memory_order_acquire);
  }
  size_t acquireReadIdx() const noexcept {
    return read_idx_.load(std::memory_order_acquire);
  }
  void publishWriteIdx(size_t next) noexcept {
    write_idx_.store(next, std::memory_order_release);
  }
  void publishReadIdx(size_t next) noexcept {
    read_idx_.store(next, std::memory_order_release);
  }

 private:
  const size_t capacity_;
  const size_t mask_;
  std::unique_ptr<std::optional<T>[]> slots_;

  ABSL_CACHELINE_ALIGNED std::atomic<size_t> write_idx_{0};
  mutable size_t read_idx_cache_{0};

  ABSL_CACHELINE_ALIGNED std::atomic<size_t> read_idx_{0};
  mutable size_t write_idx_cache_{0};
};

// A dynamically growing, lock-free single-producer/single-consumer (SPSC) FIFO
// queue built by chaining `BoundedSpscQueue<T>` segments. Wraps in-place
// within a single ring in steady state, allocates additional segments on
// bursts so `Enqueue` never fails, and caches one drained spare segment to
// avoid repeated allocations.
//
// Thread-safety: One producer (`IsFull`, `Enqueue`, `TryEnqueue`) and one
// consumer (`IsEmpty`, `Dequeue`) may operate concurrently without locks.
template <typename T>
class UnboundedSpscQueue final {
 public:
  static constexpr size_t kDefaultSegmentCapacity = 1024;

  explicit UnboundedSpscQueue(size_t segment_capacity = kDefaultSegmentCapacity)
      : seg_cap_(std::bit_ceil(segment_capacity == 0 ? size_t{1}
                                                     : segment_capacity)) {
    auto* initial = new Segment(seg_cap_);
    tail_seg_ = initial;
    head_seg_ = initial;
  }

  DISALLOW_COPY(UnboundedSpscQueue);
  DISALLOW_MOVE(UnboundedSpscQueue);

  ~UnboundedSpscQueue() {
    Segment* seg = head_seg_;
    while (seg != nullptr) {
      Segment* next = seg->next.load(std::memory_order_relaxed);
      delete seg;
      seg = next;
    }
    delete spare_seg_.load(std::memory_order_relaxed);
  }

  // Returns the usable slot capacity per segment (always a power of two).
  size_t SegmentCapacity() const noexcept { return seg_cap_; }

  // Returns an approximate number of queued elements.
  size_t SizeApprox() const noexcept {
    const size_t w = total_pushed_.load(std::memory_order_acquire);
    const size_t r = total_popped_.load(std::memory_order_acquire);
    return w >= r ? (w - r) : 0;
  }

  // Producer-only: always returns false since the queue grows dynamically.
  bool IsFull() const noexcept { return false; }

  // Consumer-only: returns true iff the queue is currently empty.
  bool IsEmpty() const noexcept {
    if (!head_seg_->ring.IsEmpty()) return false;
    return acquireNext(head_seg_) == nullptr;
  }

  // Producer-only: copies `value` into the queue.
  void Enqueue(const T& value) { emplace(value); }

  // Producer-only: moves `value` into the queue.
  void Enqueue(T&& value) { emplace(std::move(value)); }

  // Producer-only: copies `value` into the queue and returns true.
  bool TryEnqueue(const T& value) {
    emplace(value);
    return true;
  }

  // Producer-only: moves `value` into the queue and returns true.
  bool TryEnqueue(T&& value) {
    emplace(std::move(value));
    return true;
  }

  // Consumer-only: moves the oldest element into `out` and destroys its slot.
  // Returns false if the queue is empty.
  bool Dequeue(T& out) {
    if (ABSL_PREDICT_TRUE(head_seg_->ring.Dequeue(out))) {
      recordPop();
      return true;
    }

    Segment* next_seg = acquireNext(head_seg_);
    if (next_seg == nullptr) return false;

    // Re-check `head_seg_->ring` after acquiring `next_seg` in case the
    // producer pushed into `head_seg_->ring` and overflowed to `next_seg`
    // after the initial `Dequeue` check above.
    if (head_seg_->ring.Dequeue(out)) {
      recordPop();
      return true;
    }

    recycleSpare(std::exchange(head_seg_, next_seg));
    (void)head_seg_->ring.Dequeue(out);
    recordPop();
    return true;
  }

 private:
  struct Segment {
    explicit Segment(size_t cap) : ring(cap) {}

    BoundedSpscQueue<T> ring;
    ABSL_CACHELINE_ALIGNED std::atomic<Segment*> next{nullptr};
  };

  // Producer-only: stores `value` at the back of the queue, chaining a new
  // segment when the tail segment is full.
  template <typename U>
  void emplace(U&& value) {
    if (ABSL_PREDICT_TRUE(!tail_seg_->ring.IsFull())) {
      (void)tail_seg_->ring.TryEnqueue(std::forward<U>(value));
    } else {
      std::unique_ptr<Segment> next_seg = takeSpare();
      if (next_seg == nullptr) {
        next_seg = std::make_unique<Segment>(seg_cap_);
      }
      (void)next_seg->ring.TryEnqueue(std::forward<U>(value));
      Segment* raw_next = next_seg.release();
      publishNext(tail_seg_, raw_next);
      tail_seg_ = raw_next;
    }
    recordPush();
  }

  static Segment* acquireNext(const Segment* seg) noexcept {
    return seg->next.load(std::memory_order_acquire);
  }
  static void publishNext(Segment* seg, Segment* next) noexcept {
    seg->next.store(next, std::memory_order_release);
  }

  std::unique_ptr<Segment> takeSpare() noexcept {
    return std::unique_ptr<Segment>(
        spare_seg_.exchange(nullptr, std::memory_order_acq_rel));
  }
  void recycleSpare(Segment* drained_head) noexcept {
    drained_head->next.store(nullptr, std::memory_order_relaxed);
    delete spare_seg_.exchange(drained_head, std::memory_order_acq_rel);
  }

  void recordPush() noexcept {
    total_pushed_.store(total_pushed_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
  }
  void recordPop() noexcept {
    total_popped_.store(total_popped_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
  }

 private:
  const size_t seg_cap_;

  ABSL_CACHELINE_ALIGNED Segment* tail_seg_ = nullptr;
  std::atomic<size_t> total_pushed_{0};

  ABSL_CACHELINE_ALIGNED Segment* head_seg_ = nullptr;
  std::atomic<size_t> total_popped_{0};

  ABSL_CACHELINE_ALIGNED std::atomic<Segment*> spare_seg_{nullptr};
};

// Default unbounded lock-free SPSC queue alias.
template <typename T>
using SpscQueue = UnboundedSpscQueue<T>;

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_SPSC_QUEUE_H_
