#include "peregrine/src/internal/channel/channel_tcp.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace peregrine::internal {

std::string TcpChannel::ToString() const {
  return absl::StrCat("TcpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
