#ifndef PEREGRINE_SRC_INTERNAL_EVENT_POLLER_H_
#define PEREGRINE_SRC_INTERNAL_EVENT_POLLER_H_

#include <sys/epoll.h>

#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

// This class implements an event poller using epoll.
// It is thread-compatible but not thread-safe.
class Poller {
 public:
  // Creates an event poller.
  static std::unique_ptr<Poller> Create();

  // Adds a file descriptor to be watched.
  bool Register(fd_t fd, uint32_t events);

  // Removes a file descriptor from being watched.
  bool Unregister(fd_t fd);

  // Waits blockingly for events on the file descriptors registered.
  // Returns the number of file descriptors with events received.
  int BlockingWait(epoll_event* events, int max_events);

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
