#include "src/internal/base/hostinfo.h"

#include <algorithm>

#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

bool HostInfo::IsValid() const {
  return control_plane_listener.IsValid() && !data_plane_listeners.empty() &&
         std::all_of(data_plane_listeners.begin(), data_plane_listeners.end(),
                     [](const Endpoint& e) { return e.IsValid(); });
}

}  // namespace peregrine::internal
