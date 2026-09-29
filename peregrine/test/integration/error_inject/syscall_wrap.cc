#include "peregrine/test/integration/error_inject/syscall_wrap.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <atomic>

#include "peregrine/test/integration/error_inject/error_injector.h"

namespace peregrine::integration {

namespace {
std::atomic<ErrorInjector*> active_injector{nullptr};
}  // namespace

void SetActiveErrorInjector(ErrorInjector* injector) {
  active_injector.store(injector, std::memory_order_release);
}

ErrorInjector* GetActiveErrorInjector() {
  return active_injector.load(std::memory_order_acquire);
}

}  // namespace peregrine::integration

extern "C" ssize_t __real_sendmsg(int fd, const struct msghdr* msg, int flags);

extern "C" ssize_t __wrap_sendmsg(int fd, const struct msghdr* msg, int flags) {
  if (auto* injector = peregrine::integration::GetActiveErrorInjector();
      injector != nullptr) {
    return injector->OnSendMsg(fd, msg, flags, &__real_sendmsg);
  }
  return __real_sendmsg(fd, msg, flags);
}
