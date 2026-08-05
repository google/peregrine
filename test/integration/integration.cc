#include "test/integration/integration.h"

#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
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

  const std::string sndr_ctrl_ep = CreateEndpoint(AF_INET);
  const std::string sndr_data_ep = CreateEndpoint(AF_INET);
  const std::string rcvr_ctrl_ep = CreateEndpoint(AF_INET);
  const std::string rcvr_data_ep = CreateEndpoint(AF_INET);

  controlpath_sndr_ = std::make_unique<ControlpathHost>(
      "Sender Control Path", sndr_ctrl_ep, rcvr_ctrl_ep, "gRPC", "CONNECTED");
  controlpath_rcvr_ = std::make_unique<ControlpathHost>(
      "Receiver Control Path", rcvr_ctrl_ep, "", "gRPC", "LISTENING");

  datapath_sndr_ = std::make_unique<DatapathHost>(
      "Sender Data Path", sndr_ctrl_ep, sndr_data_ep, rcvr_ctrl_ep,
      rcvr_data_ep, kBufSize);
  datapath_rcvr_ = std::make_unique<DatapathHost>(
      "Receiver Data Path", rcvr_ctrl_ep, rcvr_data_ep, "", "", kBufSize);
}

PeregrineIntegration::Stats PeregrineIntegration::GetStats() const {
  Stats stats;
  stats.transfers_completed = datapath_sndr_->n_transfers();
  stats.bytes_transferred = datapath_sndr_->n_bytes();

  const absl::Duration elapsed = absl::Now() - settings_.test_begin;
  const double sec = absl::ToDoubleSeconds(elapsed);
  if (sec > 0.0) {
    stats.throughput_mbps =
        (static_cast<double>(stats.bytes_transferred) / (1024.0 * 1024.0)) /
        sec;
  }
  return stats;
}

void PeregrineIntegration::Run() {
  datapath_sndr_->GenData();
  datapath_rcvr_->ClearData();

  Wait();
}

void PeregrineIntegration::Wait() {
  while (Continue()) {
    SendRequest();
  }
}

bool PeregrineIntegration::Continue() const {
  return !stop_.load() &&
         absl::Now() < settings_.test_begin + settings_.test_duration;
}

void PeregrineIntegration::SendRequest() {
  Request req = {
      .op = Op::kWrite,
      .laddr = datapath_sndr_->DataPtr(),
      .raddr = datapath_rcvr_->DataPtr(),
      .len = datapath_sndr_->DataSize(),
  };

  auto handle_or = datapath_sndr_->Post(controlpath_rcvr_->endpoint(), {req});
  if (!handle_or.ok()) {
    absl::SleepFor(absl::Microseconds(50));
    return;
  }

  Handle h = handle_or.value();
  while (Continue()) {
    auto status_or = datapath_sndr_->Poll(h);
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
