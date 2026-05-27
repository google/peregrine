#include "src/internal/util/test_util.h"

#include "absl/log/check.h"
#include "src/internal/base/types.h"
#include "util/util.h"

namespace peregrine::testing {

port_t TestOnly_FindFreeTcpPort(int family) {
  const port_t port = util::FindFreePort(family, /*kTcp=*/true);
  CHECK_GT(port, 0);  // Crash OK
  return port;
}

port_t TestOnly_FindFreeUdpPort(int family) {
  const port_t port = util::FindFreePort(family, /*kTcp=*/false);
  CHECK_GT(port, 0);  // Crash OK
  return port;
}

}  // namespace peregrine::testing
