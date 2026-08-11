#ifndef PEREGRINE_TEST_INTEGRATION_CONTROLPATH_HOST_H_
#define PEREGRINE_TEST_INTEGRATION_CONTROLPATH_HOST_H_

#include <string>

#include "absl/strings/string_view.h"
#include "test/integration/metrics.h"

namespace peregrine::integration {

// Control plane host for Peregrine integration test.
class ControlpathHost final {
 public:
  ControlpathHost(Component c, absl::string_view endpoint,
                  absl::string_view peer_endpoint, absl::string_view mode,
                  absl::string_view status);
  ~ControlpathHost() = default;

  const std::string& endpoint() const { return endpoint_; }
  const std::string& peer_endpoint() const { return peer_endpoint_; }

 private:
  const std::string endpoint_;
  const std::string peer_endpoint_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_CONTROLPATH_HOST_H_
