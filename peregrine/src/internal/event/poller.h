#ifndef PEREGRINE_SRC_INTERNAL_EVENT_POLLER_H_
#define PEREGRINE_SRC_INTERNAL_EVENT_POLLER_H_

#include <sys/epoll.h>

#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// This class implements an event poller using epoll.
// It is thread-safe since there is no mutable state.
class Poller {
 public:
  // Creates an event poller.
  static std::unique_ptr<Poller> Create();

  // Disallow copy and move.
  DISALLOW_COPY(Poller);
  DISALLOW_MOVE(Poller);

  // Adds a file descriptor to be watched.
  // Returns 0 on success, -1 on error.
  int Register(fd_t fd, uint32_t events);

  // Removes a file descriptor from being watched.
  // Returns 0 on success, -1 on error.
  int Unregister(fd_t fd);

  // Waits blockingly for events on the file descriptors registered.
  // Returns the number of file descriptors with events received.
  // Returns 0 on timeout or signal interruption, or -1 on error.
  int BlockingWait(epoll_event* events, int max_events, int timeout_ms);

  // Destructor closes the epoll file descriptor.
  ~Poller();

 private:
  // Constructor with a valid epoll file descriptor.
  explicit Poller(fd_t epoll_fd) : epoll_fd_(epoll_fd) { DCHECK(invariant()); }

  // Returns true iff the invariant holds.
  bool invariant() const { return epoll_fd_.value() >= 0; }

 private:
  const fd_t epoll_fd_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_EVENT_POLLER_H_
