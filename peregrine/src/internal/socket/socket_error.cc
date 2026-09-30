#include "peregrine/src/internal/socket/socket_error.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <limits>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

namespace {
using util::Errno;
}  // namespace

int GetSocketError(const fd_t fd) {
  int error = std::numeric_limits<int>::min();
  DCHECK_LT(error, -1);
  socklen_t len = sizeof(error);
  const int ret = GetSocketOption(fd, SO_ERROR, &error, &len);
  if ABSL_PREDICT_FALSE (ret < 0) {
    const Errno err(errno);
    LOG(WARNING) << ErrorMsg("getsockopt", err);
    return err.value();
  }
  return error;
}

}  // namespace peregrine::internal
