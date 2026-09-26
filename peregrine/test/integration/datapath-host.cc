#include "peregrine/test/integration/datapath-host.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/api/transport_util.h"
#include "peregrine/src/util/util.h"
#include "peregrine/test/integration/metrics.h"

namespace peregrine::integration {

DatapathHost::DatapathHost(Component c, std::string_view control_ep,
                           std::string_view data_ep,
                           std::string_view peer_control_ep,
                           std::string_view peer_data_ep, size_t buf_size,
                           int num_conns)
    : control_endpoint_(control_ep),
      data_endpoint_(data_ep),
      peer_control_endpoint_(peer_control_ep),
      peer_data_endpoint_(peer_data_ep),
      data_(buf_size),
      transport_(CreateTransport(control_ep, TransportType::kTcp, num_conns)) {
  DCHECK_GT(buf_size, 0);
  CHECK_NE(transport_, nullptr);
  Metrics::SetDatapathInfo(c, data_ep, peer_data_ep, "ACTIVE");
}

void DatapathHost::ClearData(Byte* addr, size_t len) {
  DCHECK_GE(addr, data_.data());
  DCHECK_LE(addr + len, data_.data() + data_.size());
  std::fill_n(addr, len, 0);
}

void DatapathHost::GenData() { util::RandomNonZero(absl::MakeSpan(data_)); }

}  // namespace peregrine::integration
