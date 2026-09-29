#ifndef PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECTOR_H_
#define PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECTOR_H_

#include <sys/socket.h>
#include <sys/types.h>

#include <cstdint>
#include <string_view>

namespace peregrine::integration {

// Error injection strategy type.
enum class ErrorInjectorType {
  kNone,
  kMidChunkDisconnect,
};

using SendMsgFn = ssize_t (*)(int fd, const struct msghdr* msg, int flags);

// Abstract interface for integration test error injectors.
class ErrorInjector {
 public:
  virtual ~ErrorInjector() = default;

  // Name of the error injector (e.g. "mid_chunk_disconnect").
  virtual std::string_view Name() const = 0;

  // Total number of faults injected so far.
  virtual uint64_t InjectedCount() const = 0;

  // Intercepts a sendmsg() syscall and either injects a fault or forwards to
  // `real_sendmsg`.
  virtual ssize_t OnSendMsg(int fd, const struct msghdr* msg, int flags,
                            SendMsgFn real_sendmsg) = 0;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECTOR_H_
