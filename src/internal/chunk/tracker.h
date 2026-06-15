#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_TRACKER_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_TRACKER_H_

#include <cstdint>
#include <string>

#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

// `Tracker` defines an interface to track all the chunks of a buffer.
class Tracker {
 public:
  // Destructor.
  virtual ~Tracker() = default;

  // Returns the total number of chunks.
  virtual uint32_t TotalNumChunks() const = 0;

  // Returns true iff the `index`-th chunk is being busy written.
  virtual bool IsBusy(chunk_t index) const = 0;

  // Returns true iff no chunk has been written yet.
  virtual bool IsEmpty() const = 0;

  // Returns true iff all the chunks have been written successfully.
  virtual bool IsCompleted() const = 0;

  // Gets the exclusive data write access to the `index`-th chunk.
  // Returns true if the permission is granted.
  virtual bool Acquire(chunk_t index) = 0;

  // Releases the exclusive data write access to the `index`-th chunk.
  // PRECONDITION: The caller must have called Acquire() and it returned true.
  virtual void Release(chunk_t index, bool success) = 0;

  // Returns a string representation of the tracker.
  virtual std::string ToString() const = 0;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_TRACKER_H_
