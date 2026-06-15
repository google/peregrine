#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_COUNTER_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_COUNTER_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/tracker.h"

namespace peregrine::internal {

// `ChunkCounter` is a `Tracker` that counts the number of bytes.
// It is thread-compatible but not thread-safe.
class ChunkCounter final : public Tracker {
 public:
  // Constructor.
  explicit ChunkCounter(uint32_t total_num_chunks)
      : total_num_chunks_(total_num_chunks) {}

  // Returns the total number of chunks.
  uint32_t TotalNumChunks() const override { return total_num_chunks_; }

  // Returns true iff the `index`-th chunk is being busy written.
  constexpr bool IsBusy(chunk_t index) const override { return false; }

  // Returns true iff no chunk has been written yet.
  bool IsEmpty() const override { return numChunks() == 0; }

  // Returns true iff all the chunks have been written successfully.
  bool IsCompleted() const override { return numChunks() == total_num_chunks_; }

  // Gets the exclusive data write access to the `index`-th chunk.
  constexpr bool Acquire(chunk_t index) override { return true; }

  // Releases the exclusive data write access to the `index`-th chunk.
  void Release(chunk_t index, bool success) override {
    DCHECK(Acquire(index));
    incNumChunks();
  }

  // Returns a string representation of the tracker.
  std::string ToString() const override {
    return absl::StrFormat("Chunks(done/total)=%d/%d", numChunks(),
                           total_num_chunks_);
  }

 private:
  // Returns the number of chunks.
  uint32_t numChunks() const {
    return num_chunks_.load(std::memory_order_acquire);
  }

  // Increments the number of chunks.
  void incNumChunks() { num_chunks_.fetch_add(1, std::memory_order_release); }

 private:
  const uint32_t total_num_chunks_;
  std::atomic<uint32_t> num_chunks_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_COUNTER_H_
