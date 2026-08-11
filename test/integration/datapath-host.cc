#include "test/integration/datapath-host.h"

#include <cstddef>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"
#include "test/integration/metrics.h"

namespace peregrine::integration {

DatapathHost::DatapathHost(Component c, absl::string_view control_ep,
                           absl::string_view data_ep,
                           absl::string_view peer_control_ep,
                           absl::string_view peer_data_ep, size_t buf_size,
                           int num_conns)
    : control_endpoint_(control_ep),
      data_endpoint_(data_ep),
      peer_control_endpoint_(peer_control_ep),
      peer_data_endpoint_(peer_data_ep),
      data_(buf_size),
      transport_(CreateTransport(absl::StrCat(control_ep, ",", data_ep),
                                 num_conns)) {
  DCHECK_GT(buf_size, 0);
  CHECK_NE(transport_, nullptr);
  Metrics::SetDatapathInfo(c, data_ep, peer_data_ep, "ACTIVE");
}

void DatapathHost::GenData() { util::RandomNonZero(absl::MakeSpan(data_)); }

}  // namespace peregrine::integration
