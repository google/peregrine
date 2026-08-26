#include "test/integration/controlpath-host.h"

#include <string>
#include <string_view>

#include "test/integration/metrics.h"

namespace peregrine::integration {

ControlpathHost::ControlpathHost(Component c, std::string_view endpoint,
                                 std::string_view peer_endpoint,
                                 std::string_view mode, std::string_view status)
    : endpoint_(endpoint), peer_endpoint_(peer_endpoint) {
  Metrics::SetControlpathInfo(c, endpoint, peer_endpoint, mode, status);
}

}  // namespace peregrine::integration
