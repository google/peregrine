#include "peregrine/test/integration/integration.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/util/util.h"
#include "peregrine/test/integration/controlpath-host.h"
#include "peregrine/test/integration/datapath-host.h"
#include "peregrine/test/integration/flags.h"
#include "peregrine/test/integration/metrics.h"
#include "peregrine/test/integration/settings.h"
#include "peregrine/test/workloads/workload_generator.h"
#include "peregrine/test/workloads/workload_util.h"

namespace peregrine::integration {

namespace {
constexpr std::string_view kLocalhost = "127.0.0.1";

std::string CreateEndpoint(int family) {
  const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
  return absl::StrCat(kLocalhost, ":", port);
}

uint64_t TotalTransferSize(absl::Span<const Request> requests) {
  uint64_t total = 0;
  for (const Request& req : requests) {
    total += req.len;
  }
  return total;
}
}  // namespace

PeregrineIntegration::PeregrineIntegration() {
  flags_ = ReadFlags();
  settings_.test_begin = absl::Now();

  workload_ = workloads::CreateWorkload(flags_.workload);
  CHECK(workload_ != nullptr) << "Failed to create workload generator";
  const uint64_t buffer_size = workload_->TotalSizeBytes();

  const std::string sndr_ctrl = CreateEndpoint(AF_INET);
  const std::string sndr_data = CreateEndpoint(AF_INET);
  const std::string rcvr_ctrl = CreateEndpoint(AF_INET);
  const std::string rcvr_data = CreateEndpoint(AF_INET);

  controlpath_sndr_ = std::make_unique<ControlpathHost>(
      Component::kSenderControlpath, sndr_ctrl, rcvr_ctrl, "gRPC",
      "CONNECTED");
  controlpath_rcvr_ = std::make_unique<ControlpathHost>(
      Component::kReceiverControlpath, rcvr_ctrl, "", "gRPC", "LISTENING");

  datapath_sndr_ = std::make_unique<DatapathHost>(
      Component::kSenderDatapath, sndr_ctrl, sndr_data, rcvr_ctrl, rcvr_data,
      buffer_size, flags_.conns_per_peer);
  datapath_rcvr_ = std::make_unique<DatapathHost>(
      Component::kReceiverDatapath, rcvr_ctrl, rcvr_data, "", "", buffer_size,
      flags_.conns_per_peer);
}

void PeregrineIntegration::CollectMetrics() const {
  if (datapath_sndr_ != nullptr) {
    Metrics::UpdateTransportMetrics(Component::kSenderDatapath,
                                    datapath_sndr_->GetTransportMetrics());
  }
  if (datapath_rcvr_ != nullptr) {
    Metrics::UpdateTransportMetrics(Component::kReceiverDatapath,
                                    datapath_rcvr_->GetTransportMetrics());
  }
}

PeregrineIntegration::Stats PeregrineIntegration::GetStats() const {
  Stats stats;
  stats.transfers_completed =
      Metrics::GetTransfers(Component::kSenderDatapath);
  stats.bytes_transferred = Metrics::GetBytes(Component::kSenderDatapath);

  const absl::Duration elapsed = absl::Now() - settings_.test_begin;
  const double nsec = absl::ToDoubleNanoseconds(elapsed);
  if (nsec > 0.0) {
    stats.throughput_gbps = stats.bytes_transferred * 8.0 / nsec;
  }
  return stats;
}

bool PeregrineIntegration::shouldContinue() const {
  return !stop_.load() &&
         absl::Now() < settings_.test_begin + flags_.test_duration;
}

void PeregrineIntegration::Run() {
  datapath_sndr_->GenData();
  datapath_rcvr_->ClearData();
  while (shouldContinue()) {
    sendRequest();
  }
}

void PeregrineIntegration::sendRequest() {
  const std::vector<Request> requests = workload_->GenerateRequests(
      datapath_sndr_->DataPtr(), datapath_rcvr_->DataPtr());
  if (requests.empty()) {
    Stop();
    return;
  }

  clearData(requests);

  const absl::StatusOr<Handle> handle_or =
      datapath_sndr_->Post(controlpath_rcvr_->endpoint(), requests,
                           /*on_complete=*/nullptr);
  if (!handle_or.ok()) {
    absl::SleepFor(absl::Microseconds(50));
    return;
  }

  const Handle h = handle_or.value();
  while (shouldContinue()) {
    const auto status_or = datapath_sndr_->Poll(h);
    if (!status_or.ok()) break;

    const Status s = status_or.value();
    if (IsCompleted(s)) {
      if (s == Status::kSuccess) {
        const uint64_t xfer_size = TotalTransferSize(requests);
        Metrics::IncrementTransfers(Component::kSenderDatapath, xfer_size);
        Metrics::IncrementTransfers(Component::kReceiverDatapath, xfer_size);
        verifyData(requests);
      }
      break;
    }

    absl::SleepFor(absl::Microseconds(50));
  }
}

void PeregrineIntegration::clearData(absl::Span<const Request> requests) {
  if (!flags_.verify_data) return;
  for (const Request& req : requests) {
    if (req.op == Op::kWrite) {
      datapath_rcvr_->ClearData(req.raddr, req.len);
    } else {
      datapath_sndr_->ClearData(req.laddr, req.len);
    }
  }
}

void PeregrineIntegration::verifyData(
    absl::Span<const Request> requests) const {
  if (!flags_.verify_data) return;
  for (const Request& req : requests) {
    CHECK(absl::MakeConstSpan(req.laddr, req.len) ==
          absl::MakeConstSpan(req.raddr, req.len))
        << "Data integrity verification failed for request: " << req;
  }
}

}  // namespace peregrine::integration
