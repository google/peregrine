#include "test/integration/controlpath-host.h"

#include <string>

#include "absl/strings/string_view.h"
#include "test/integration/metrics.h"

namespace peregrine::integration {

ControlpathHost::ControlpathHost(Component c,
                                 absl::string_view endpoint,
                                 absl::string_view peer_endpoint,
                                 absl::string_view mode,
                                 absl::string_view status)
    : endpoint_(endpoint),
      peer_endpoint_(peer_endpoint) {
  Metrics::SetControlpathInfo(c, endpoint, peer_endpoint, mode, status);
}

}  // namespace peregrine::integration
