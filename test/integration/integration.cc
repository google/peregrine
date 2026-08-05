#include "test/integration/integration.h"

#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport_types.h"
#include "src/util/util.h"
#include "test/integration/controlpath-host.h"
#include "test/integration/datapath-host.h"
#include "test/integration/flags.h"

namespace peregrine::integration {

namespace {
constexpr absl::string_view kLocalhost = "127.0.0.1";

std::string CreateEndpoint(int family) {
  const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
  return absl::StrCat(kLocalhost, ":", port);
}
}  // namespace

PeregrineIntegration::PeregrineIntegration() {
  constexpr size_t kBufSize = 1024 * 1024;  // 1 MB

  settings_.test_begin = absl::Now();
  settings_.test_duration =
      std::max(absl::Seconds(1), absl::GetFlag(FLAGS_test_duration));

  const std::string sndr_ctrl = CreateEndpoint(AF_INET);
  const std::string sndr_data = CreateEndpoint(AF_INET);
  const std::string rcvr_ctrl = CreateEndpoint(AF_INET);
  const std::string rcvr_data = CreateEndpoint(AF_INET);

  controlpath_sndr_ = std::make_unique<ControlpathHost>(
      "Sender Control Path", sndr_ctrl, rcvr_ctrl, "gRPC", "CONNECTED");
  controlpath_rcvr_ = std::make_unique<ControlpathHost>(
      "Receiver Control Path", rcvr_ctrl, "", "gRPC", "LISTENING");

  datapath_sndr_ = std::make_unique<DatapathHost>(
      "Sender Data Path", sndr_ctrl, sndr_data, rcvr_ctrl, rcvr_data, kBufSize);
  datapath_rcvr_ = std::make_unique<DatapathHost>(
      "Receiver Data Path", rcvr_ctrl, rcvr_data, "", "", kBufSize);
}

PeregrineIntegration::Stats PeregrineIntegration::GetStats() const {
  Stats stats;
  stats.transfers_completed = datapath_sndr_->n_transfers();
  stats.bytes_transferred = datapath_sndr_->n_bytes();

  const absl::Duration elapsed = absl::Now() - settings_.test_begin;
  const double nsec = absl::ToDoubleNanoseconds(elapsed);
  if (nsec > 0.0) {
    stats.throughput_gbps = stats.bytes_transferred * 8.0 / nsec;
  }
  return stats;
}

bool PeregrineIntegration::shouldContinue() const {
  return !stop_.load() &&
         absl::Now() < settings_.test_begin + settings_.test_duration;
}

void PeregrineIntegration::Run() {
  datapath_sndr_->GenData();
  datapath_rcvr_->ClearData();
  while (shouldContinue()) {
    sendRequest();
  }
}

void PeregrineIntegration::sendRequest() {
  const Request req = {
      .op = Op::kWrite,
      .laddr = datapath_sndr_->DataPtr(),
      .raddr = datapath_rcvr_->DataPtr(),
      .len = datapath_sndr_->DataSize(),
  };
  const absl::StatusOr<Handle> handle_or =
      datapath_sndr_->Post(controlpath_rcvr_->endpoint(), {req});
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
        datapath_sndr_->IncrementTransfers(datapath_sndr_->DataSize());
        datapath_rcvr_->IncrementTransfers(datapath_sndr_->DataSize());
      }
      break;
    }
    absl::SleepFor(absl::Microseconds(50));
  }
}

}  // namespace peregrine::integration
