#include "peregrine/src/internal/channel/channel_udp.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace peregrine::internal {

std::string UdpChannel::ToString() const {
  return absl::StrCat("UdpChannel: ", socket_->ToString());
}

}  // namespace peregrine::internal
