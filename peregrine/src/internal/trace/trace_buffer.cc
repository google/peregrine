#include "peregrine/src/internal/trace/trace_buffer.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

TraceBuffer::TraceBuffer(size_t capacity)
    : capacity_(normalizeCapacity(capacity)),
      mask_(capacity_ - 1),
      events_(capacity_),
      write_idx_(0) {}

std::vector<TraceEvent> TraceBuffer::Snapshot() const {
  const uint64_t total = write_idx_.load(std::memory_order_acquire);
  if (total == 0) return {};

  const uint64_t count = std::min<uint64_t>(total, capacity_);
  const uint64_t start = total - count;
  std::vector<TraceEvent> out;
  out.reserve(count);
  for (uint64_t i = 0; i < count; ++i) {
    out.push_back(events_[(start + i) & mask_].LoadAcquire());
  }
  return out;
}

void TraceBuffer::Clear() noexcept {
  write_idx_.store(0, std::memory_order_release);
  for (size_t i = 0; i < capacity_; ++i) {
    events_[i].Clear();
  }
}

void TraceBuffer::Slot::StoreRelaxed(const TraceEvent& ev) noexcept {
  uint64_t raw[kNumWords] = {};
  std::memcpy(raw, &ev, sizeof(ev));
  for (size_t i = 0; i < kNumWords - 1; ++i) {
    words[i].store(raw[i], std::memory_order_relaxed);
  }
  words[kNumWords - 1].store(raw[kNumWords - 1], std::memory_order_release);
}

TraceEvent TraceBuffer::Slot::LoadAcquire() const noexcept {
  uint64_t raw[kNumWords] = {};
  raw[kNumWords - 1] = words[kNumWords - 1].load(std::memory_order_acquire);
  for (size_t i = 0; i < kNumWords - 1; ++i) {
    raw[i] = words[i].load(std::memory_order_relaxed);
  }
  TraceEvent ev;
  std::memcpy(&ev, raw, sizeof(ev));
  return ev;
}

void TraceBuffer::Slot::Clear() noexcept {
  for (auto& w : words) {
    w.store(0, std::memory_order_relaxed);
  }
}

size_t TraceBuffer::normalizeCapacity(size_t capacity) noexcept {
  const size_t clamped = std::max<size_t>(capacity, 16);
  return std::bit_ceil(clamped);
}

}  // namespace peregrine::internal
