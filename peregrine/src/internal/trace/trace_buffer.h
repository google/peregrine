#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_BUFFER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_BUFFER_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "peregrine/src/internal/coding_style.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// Pre-allocated ring buffer for `TraceEvent` records supporting lock-free
// atomic updates and zero heap allocation on `Record()`.
class TraceBuffer final {
  static_assert(coding_style::kClassPrivateFunctionNamesStartWithLowercase);
  static_assert(coding_style::kClassLastPrivateBlockHasAllNonStaticDataMembers);

 public:
  static constexpr size_t kDefaultCapacity = 32768;

  explicit TraceBuffer(size_t capacity = kDefaultCapacity);

  DISALLOW_COPY(TraceBuffer);
  DISALLOW_MOVE(TraceBuffer);

  ~TraceBuffer() = default;

  // Returns the ring buffer capacity (always a power of two).
  size_t Capacity() const noexcept { return capacity_; }

  // Returns the total number of events recorded since creation or `Clear()`.
  // May exceed `Capacity()`, in which case older events are overwritten.
  uint64_t TotalRecorded() const noexcept {
    return write_idx_.load(std::memory_order_acquire);
  }

  // Appends a trace event into the ring buffer without locks or allocations.
  void Record(const TraceEvent& ev) noexcept {
    const uint64_t idx = write_idx_.fetch_add(1, std::memory_order_relaxed);
    events_[idx & mask_].StoreRelaxed(ev);
  }

  // Returns a chronological snapshot of valid events currently in the buffer.
  // Not atomic across slots or words; may race with concurrent `Record()` or
  // `Clear()` calls.
  std::vector<TraceEvent> Snapshot() const;

  // Resets the ring buffer.
  // Not atomic across slots; may race with concurrent `Record()` or
  // `Snapshot()` calls.
  void Clear() noexcept;

 private:
  struct Slot final {
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(sizeof(TraceEvent) % sizeof(uint64_t) == 0);
    static constexpr size_t kNumWords = sizeof(TraceEvent) / sizeof(uint64_t);
    std::array<std::atomic<uint64_t>, kNumWords> words{};

    void StoreRelaxed(const TraceEvent& ev) noexcept;
    TraceEvent LoadAcquire() const noexcept;
    void Clear() noexcept;
  };
  static_assert(sizeof(Slot) == sizeof(TraceEvent));

  static size_t normalizeCapacity(size_t capacity) noexcept;

 private:
  const size_t capacity_;
  const size_t mask_;
  std::vector<Slot> events_;
  std::atomic<uint64_t> write_idx_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_BUFFER_H_
