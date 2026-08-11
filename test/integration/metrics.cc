#include "test/integration/metrics.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "test/integration/settings.h"

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

absl::string_view ComponentName(Component c) {
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

}  // namespace

void Metrics::SetControlpathInfo(Component c, absl::string_view endpoint,
                                 absl::string_view peer_endpoint,
                                 absl::string_view mode,
                                 absl::string_view status) {
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

void Metrics::SetDatapathInfo(Component c, absl::string_view endpoint,
                              absl::string_view peer_endpoint,
                              absl::string_view status) {
  absl::MutexLock lock(GetMutex());
  DatapathMetrics* dp = GetDatapath(c, GetState());
  if (dp != nullptr) {
    dp->endpoint = std::string(endpoint);
    dp->peer_endpoint = std::string(peer_endpoint);
    dp->status = std::string(status);
    dp->active = true;
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
  absl::string_view name = ComponentName(c);
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
  absl::string_view name = ComponentName(c);
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
  lines.push_back(absl::StrFormat(
      "%-15s: %d bytes (%.2f MB)", "Bytes", dp->n_bytes,
      static_cast<double>(dp->n_bytes) / (1024.0 * 1024.0)));
  return absl::StrJoin(lines, "\n");
}

void Metrics::Reset() {
  absl::MutexLock lock(GetMutex());
  GetState() = MetricsState();
}

}  // namespace peregrine::integration
