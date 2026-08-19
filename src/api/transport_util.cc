#include "src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "src/api/transport.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

using internal::HostInfo;
using internal::TransportImpl;

std::unique_ptr<Transport> CreateTransport(std::string_view endpoints,
                                           int num_conns_per_peer) {
  const HostInfo self = HostInfo::Create(endpoints);
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(WARNING) << "failed to parse: " << endpoints;
    return nullptr;
  }

  const int n = std::min(std::max(1, num_conns_per_peer), 100);
  return TransportImpl::Create(self, n);
}

}  // namespace peregrine
