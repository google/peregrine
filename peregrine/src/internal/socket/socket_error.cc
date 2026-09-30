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
  int err = std::numeric_limits<int>::min();
  DCHECK_LT(err, -1);
  socklen_t len = sizeof(err);
  const int ret = GetSocketOption(fd, SO_ERROR, &err, &len);
  if ABSL_PREDICT_FALSE (ret < 0) {
    const Errno last_errno(errno);
    LOG(WARNING) << ErrorMsg("getsockopt", last_errno);
    return last_errno.value();
  }
  return err;
}

}  // namespace peregrine::internal
