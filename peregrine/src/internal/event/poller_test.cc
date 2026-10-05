#include "peregrine/src/internal/event/poller.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

TEST(PollerTest, Basic) {
  const std::unique_ptr<Poller> poller = Poller::Create();
  ASSERT_NE(poller, nullptr);

  // Note: pipe is pollable.
  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0);
  const fd_t rfd(pipefd[0]);
  const fd_t wfd(pipefd[1]);

  constexpr uint32_t kEvents = EPOLLIN | EPOLLERR | EPOLLHUP;
  EXPECT_EQ(poller->Register(rfd, kEvents), 0);
  EXPECT_EQ(poller->Register(rfd, kEvents), -1);  // EEXIST

  // Nothing to read yet: the wait times out.
  constexpr int kMaxEvents = 4;
  epoll_event events[kMaxEvents];
  EXPECT_EQ(poller->BlockingWait(events, kMaxEvents, /*timeout_ms=*/0), 0);

  // One byte written: the read end becomes readable.
  const char byte = 'x';
  ASSERT_EQ(write(wfd.value(), &byte, 1), 1);
  ASSERT_EQ(poller->BlockingWait(events, kMaxEvents, /*timeout_ms=*/100), 1);
  EXPECT_EQ(events[0].data.fd, rfd.value());
  EXPECT_TRUE(events[0].events & EPOLLIN);

  EXPECT_EQ(poller->Unregister(rfd), 0);
  EXPECT_EQ(poller->Unregister(rfd), -1);  // ENOENT

  close(wfd.value());
  close(rfd.value());
}

}  // namespace
}  // namespace peregrine::internal::testing
