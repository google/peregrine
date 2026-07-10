#include "src/internal/event/poller.h"

#include <sys/epoll.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

TEST(PollerTest, Basic) {
  const std::unique_ptr<Poller> poller = Poller::Create();
  EXPECT_NE(poller, nullptr);

  const fd_t fd(0);
  poller->Register(fd, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET);
  poller->Unregister(fd);
}

}  // namespace
}  // namespace peregrine::internal::testing
