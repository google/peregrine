#include "peregrine/test/integration/metrics.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/test/integration/settings.h"

namespace peregrine::integration {
namespace {

struct ControlpathMetrics {
  std::string endpoint;
  std::string peer_endpoint;
  std::string mode;
  std::string status;
  bool active = false;
};

struct DatapathMetrics {
  std::string endpoint;
  std::string peer_endpoint;
  std::string status;
  int64_t n_transfers = 0;
  int64_t n_bytes = 0;
  TransportMetrics transport_metrics{};
  bool active = false;
};

struct MetricsState {
  ControlpathMetrics sndr_cp;
  ControlpathMetrics rcvr_cp;
  DatapathMetrics sndr_dp;
  DatapathMetrics rcvr_dp;
};

absl::Mutex& GetMutex() {
  static absl::NoDestructor<absl::Mutex> mutex;
  return *mutex;
}

MetricsState& GetState() {
  static absl::NoDestructor<MetricsState> state;
  return *state;
}

std::string_view ComponentName(Component c) {
  switch (c) {
    case Component::kSenderControlpath:
      return Settings::kSenderControlpathName;
    case Component::kReceiverControlpath:
      return Settings::kReceiverControlpathName;
    case Component::kSenderDatapath:
      return Settings::kSenderDatapathName;
    case Component::kReceiverDatapath:
      return Settings::kReceiverDatapathName;
  }
}

ControlpathMetrics* GetControlpath(Component c, MetricsState& s) {
  switch (c) {
    case Component::kSenderControlpath:
      return &s.sndr_cp;
    case Component::kReceiverControlpath:
      return &s.rcvr_cp;
    default:
      return nullptr;
  }
}

DatapathMetrics* GetDatapath(Component c, MetricsState& s) {
  switch (c) {
    case Component::kSenderDatapath:
      return &s.sndr_dp;
    case Component::kReceiverDatapath:
      return &s.rcvr_dp;
    default:
      return nullptr;
  }
}

double Avg(const Log2Histogram<32>& h) {
  const uint64_t count = h.Count();
  return count > 0 ? static_cast<double>(h.sum) / count : 0.0;
}

constexpr double BytesToMiB(double bytes) {
  return bytes / (1ULL << 20);
}

}  // namespace

void Metrics::SetControlpathInfo(Component c, std::string_view endpoint,
                                 std::string_view peer_endpoint,
                                 std::string_view mode,
                                 std::string_view status) {
  absl::MutexLock lock(GetMutex());
  ControlpathMetrics* cp = GetControlpath(c, GetState());
  if (cp != nullptr) {
    cp->endpoint = std::string(endpoint);
    cp->peer_endpoint = std::string(peer_endpoint);
    cp->mode = std::string(mode);
    cp->status = std::string(status);
    cp->active = true;
  }
}

void Metrics::SetDatapathInfo(Component c, std::string_view endpoint,
                              std::string_view peer_endpoint,
                              std::string_view status) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    dp->endpoint = std::string(endpoint);
    dp->peer_endpoint = std::string(peer_endpoint);
    dp->status = std::string(status);
    dp->active = true;
  }
}

void Metrics::UpdateTransportMetrics(Component c, const TransportMetrics& tm) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    dp->transport_metrics = tm;
  }
}

void Metrics::IncrementTransfers(Component c, int64_t bytes) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    dp->n_transfers++;
    dp->n_bytes += bytes;
  }
}

int64_t Metrics::GetTransfers(Component c) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    return dp->n_transfers;
  }
  return 0;
}

int64_t Metrics::GetBytes(Component c) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    return dp->n_bytes;
  }
  return 0;
}

std::string Metrics::GetControlpathDebugString(Component c) {
  absl::MutexLock lock(GetMutex());
  ControlpathMetrics* cp = GetControlpath(c, GetState());
  std::string_view name = ComponentName(c);
  if (cp == nullptr || !cp->active) {
    return absl::StrFormat("%s: disabled", name);
  }

  std::vector<std::string> lines;
  lines.push_back(absl::StrFormat("[%s]", name));
  lines.push_back(std::string(40, '-'));
  lines.push_back(absl::StrFormat("%-14s: %s", "Endpoint", cp->endpoint));
  if (!cp->peer_endpoint.empty()) {
    lines.push_back(
        absl::StrFormat("%-14s: %s", "Peer Endpoint", cp->peer_endpoint));
  }
  lines.push_back(absl::StrFormat("%-14s: %s", "Channel Mode", cp->mode));
  lines.push_back(absl::StrFormat("%-14s: %s", "Status", cp->status));
  return absl::StrJoin(lines, "\n");
}

std::string Metrics::GetDatapathDebugString(Component c) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  std::string_view name = ComponentName(c);
  if (dp == nullptr || !dp->active) {
    return absl::StrFormat("%s: disabled", name);
  }

  std::vector<std::string> lines;
  lines.push_back(absl::StrFormat("[%s]", name));
  lines.push_back(std::string(40, '-'));
  lines.push_back(absl::StrFormat("%-15s: %s", "Endpoint", dp->endpoint));
  if (!dp->peer_endpoint.empty()) {
    lines.push_back(
        absl::StrFormat("%-15s: %s", "Peer Endpoint", dp->peer_endpoint));
  }
  lines.push_back(absl::StrFormat("%-15s: %s", "Status", dp->status));
  lines.push_back(absl::StrFormat("%-15s: %d completed", "Transfers",
                                  dp->n_transfers));
  lines.push_back(absl::StrFormat("%-15s: %d bytes (%.2f MiB)", "Bytes",
                                  dp->n_bytes, BytesToMiB(dp->n_bytes)));

  const TransportMetrics& tm = dp->transport_metrics;
  lines.push_back("---- Transport Metrics ----");
  lines.push_back(
      absl::StrFormat("%-15s: %u (avg %.2f MiB)", "Write Requests",
                      tm.write.request_size_bytes.Count(),
                      BytesToMiB(Avg(tm.write.request_size_bytes))));
  lines.push_back(
      absl::StrFormat("%-15s: %u (avg %.2f MiB)", "Read Requests",
                      tm.read.request_size_bytes.Count(),
                      BytesToMiB(Avg(tm.read.request_size_bytes))));
  lines.push_back(absl::StrFormat("%-15s: %u bytes (%.2f MiB)", "Bytes Sent",
                                  tm.write.bytes, BytesToMiB(tm.write.bytes)));
  lines.push_back(
      absl::StrFormat("%-15s: avg %.2f us (%u samples)", "Write Latency",
                      Avg(tm.write.e2e_latency_us),
                      tm.write.e2e_latency_us.Count()));
  lines.push_back(
      absl::StrFormat("%-15s: %u", "Write Errors", tm.write.errors));
  // TODO(yyd): show more metrics.
  return absl::StrJoin(lines, "\n");
}

void Metrics::Reset() {
  absl::MutexLock lock(GetMutex());
  GetState() = MetricsState();
}

}  // namespace peregrine::integration
