#include "test/integration/datapath-host.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"

namespace peregrine::integration {

DatapathHost::DatapathHost(absl::string_view name, absl::string_view control_ep,
                           absl::string_view data_ep,
                           absl::string_view peer_control_ep,
                           absl::string_view peer_data_ep, size_t buf_size,
                           int num_conns)
    : name_(name),
      control_endpoint_(control_ep),
      data_endpoint_(data_ep),
      peer_control_endpoint_(peer_control_ep),
      peer_data_endpoint_(peer_data_ep),
      status_("ACTIVE"),
      data_(buf_size),
      transport_(CreateTransport(absl::StrCat(control_ep, ",", data_ep),
                                 num_conns)) {
  DCHECK_GT(buf_size, 0);
  CHECK_NE(transport_, nullptr);
}

void DatapathHost::GenData() { util::RandomNonZero(absl::MakeSpan(data_)); }

std::string DatapathHost::DebugString() const {
  std::vector<std::string> lines;
  lines.push_back(absl::StrFormat("[%s]", name_));
  lines.push_back(std::string(40, '-'));
  lines.push_back(absl::StrFormat("Endpoint       : %s", data_endpoint_));
  if (!peer_data_endpoint_.empty()) {
    lines.push_back(
        absl::StrFormat("Peer Endpoint  : %s", peer_data_endpoint_));
  }
  lines.push_back(absl::StrFormat("Status         : %s", status_));
  lines.push_back(
      absl::StrFormat("Transfers      : %d completed", n_transfers_));
  lines.push_back(
      absl::StrFormat("Bytes          : %d bytes (%.2f MB)", n_bytes_,
                      static_cast<double>(n_bytes_) / (1024.0 * 1024.0)));
  return absl::StrJoin(lines, "\n");
}

}  // namespace peregrine::integration
