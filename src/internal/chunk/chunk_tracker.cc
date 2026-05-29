#include "src/internal/chunk/chunk_tracker.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

ChunkTracker::ChunkTracker(const uint32_t total_num_chunks)
    : total_num_chunks_(total_num_chunks),
      num_chunks_done_(0),
      num_chunks_dup_(0),
      states_(total_num_chunks_, kChunkEmpty) {
  DCHECK_GE(total_num_chunks_, 1);
  DCHECK_EQ(states_.size(), total_num_chunks_);
  DCHECK(std::all_of(states_.cbegin(), states_.cend(),
                     [](const ChunkState s) { return s == kChunkEmpty; }));
}

bool ChunkTracker::IsBusy(const chunk_t index) const {
  DCHECK(isValidChunk(index));

  absl::MutexLock lock(mu_);
  return testBusyBit(states_[index.value()]);
}

bool ChunkTracker::Acquire(const chunk_t index) {
  DCHECK(isValidChunk(index));

  const chunk_t::ValueType i = index.value();
  absl::MutexLock lock(mu_);
  if (const auto s = states_[i]; ABSL_PREDICT_FALSE(testBusyBit(s))) {
    ++num_chunks_dup_;
    LOG(WARNING) << absl::StrFormat("busy chunk #%d", i);
    return false;
  } else if (s == kChunkDone) {
    return false;
  } else {
    DCHECK(s == kChunkEmpty || s == kChunkError);
    states_[i] = s | kChunkBusyFlag;
    DCHECK(testBusyBit(states_[i]));
    return true;
  }
}

bool ChunkTracker::Release(const chunk_t index, bool success) {
  DCHECK(isValidChunk(index));

  const chunk_t::ValueType i = index.value();
  absl::MutexLock lock(mu_);
  DCHECK(testBusyBit(states_[i]));
  DCHECK_NE(states_[i] & kChunkStateMask, kChunkDone);
  if ABSL_PREDICT_TRUE (success) {
    ++num_chunks_done_;
    states_[i] = kChunkDone;  // BUSY flag cleared
  } else {
    states_[i] = kChunkError;  // BUSY flag cleared
  }
  DCHECK(!testBusyBit(states_[i]));
  DCHECK(invariant());
  return total_num_chunks_ == num_chunks_done_;
}

bool ChunkTracker::IsEmpty() const {
  absl::MutexLock lock(mu_);
  DCHECK(invariant());
  return num_chunks_done_ == 0;
}

bool ChunkTracker::IsCompleted() const {
  absl::MutexLock lock(mu_);
  DCHECK(invariant());
  return total_num_chunks_ == num_chunks_done_;
}

bool ChunkTracker::invariant() const {
  const bool states_vector_size_is_fixed =
      (states_.size() == total_num_chunks_);

  const bool num_chunks_done_is_bounded =
      (0 <= num_chunks_done_ && num_chunks_done_ <= total_num_chunks_);

  const bool num_chunks_done_matches_the_chunk_states =
      (std::count(states_.begin(), states_.end(), kChunkDone) ==
       num_chunks_done_);

  return states_vector_size_is_fixed && num_chunks_done_is_bounded &&
         num_chunks_done_matches_the_chunk_states;
}

std::string ChunkTracker::ToString() const {
  absl::MutexLock lock(mu_);
  return absl::StrFormat("Chunks(done/dup/total)=%d/%d/%d", num_chunks_done_,
                         num_chunks_dup_, total_num_chunks_);
}

}  // namespace peregrine::internal
