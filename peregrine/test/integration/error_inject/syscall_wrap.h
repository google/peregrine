#ifndef PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_SYSCALL_WRAP_H_
#define PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_SYSCALL_WRAP_H_

#include "peregrine/test/integration/error_inject/error_injector.h"

namespace peregrine::integration {

// Registers or clears the active ErrorInjector used by the linker-wrapped
// syscall interceptors (__wrap_sendmsg).
void SetActiveErrorInjector(ErrorInjector* injector);

// Returns the currently registered ErrorInjector, or nullptr if none.
ErrorInjector* GetActiveErrorInjector();

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_SYSCALL_WRAP_H_
