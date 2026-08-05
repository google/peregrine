#include "test/integration/controlpath-host.h"

#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace peregrine::integration {

ControlpathHost::ControlpathHost(absl::string_view name,
                                 absl::string_view endpoint,
                                 absl::string_view peer_endpoint,
                                 absl::string_view mode,
                                 absl::string_view status)
    : name_(name),
      endpoint_(endpoint),
      peer_endpoint_(peer_endpoint),
      mode_(mode),
      status_(status) {}

std::string ControlpathHost::DebugString() const {
  std::vector<std::string> lines;
  lines.push_back(absl::StrFormat("[%s]", name_));
  lines.push_back(std::string(40, '-'));
  lines.push_back(absl::StrFormat("Endpoint      : %s", endpoint_));
  if (!peer_endpoint_.empty()) {
    lines.push_back(absl::StrFormat("Peer Endpoint : %s", peer_endpoint_));
  }
  lines.push_back(absl::StrFormat("Channel Mode  : %s", mode_));
  lines.push_back(absl::StrFormat("Status        : %s", status_));
  return absl::StrJoin(lines, "\n");
}

}  // namespace peregrine::integration
