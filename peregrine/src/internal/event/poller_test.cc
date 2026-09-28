#include "peregrine/src/internal/event/poller.h"

#include <sys/epoll.h>

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

TEST(PollerTest, Basic) {
  const std::unique_ptr<Poller> poller = Poller::Create();
  EXPECT_NE(poller, nullptr);

  const fd_t fd(0);
  constexpr uint32_t kEvents = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
  poller->Register(fd, kEvents | EPOLLET);
  poller->Unregister(fd);
}

}  // namespace
}  // namespace peregrine::internal::testing
