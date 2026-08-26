#include "test/integration/integration.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport_types.h"
#include "src/util/util.h"
#include "test/integration/controlpath-host.h"
#include "test/integration/datapath-host.h"
#include "test/integration/flags.h"
#include "test/integration/metrics.h"
#include "test/integration/settings.h"

namespace peregrine::integration {

namespace {
constexpr std::string_view kLocalhost = "127.0.0.1";

std::string CreateEndpoint(int family) {
  const uint16_t port = util::FindFreePort(family, /*tcp=*/true);
  return absl::StrCat(kLocalhost, ":", port);
}
}  // namespace

PeregrineIntegration::PeregrineIntegration() {
  flags_ = ReadFlags();
  settings_.test_begin = absl::Now();

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
      Component::kSenderDatapath, sndr_ctrl, sndr_data, rcvr_ctrl,
      rcvr_data, flags_.buffer_size, flags_.conns_per_peer);
  datapath_rcvr_ = std::make_unique<DatapathHost>(
      Component::kReceiverDatapath, rcvr_ctrl, rcvr_data, "", "",
      flags_.buffer_size, flags_.conns_per_peer);
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
  const Request req = {
      .op = Op::kWrite,
      .laddr = datapath_sndr_->DataPtr(),
      .raddr = datapath_rcvr_->DataPtr(),
      .len = datapath_sndr_->DataSize(),
  };

  if (flags_.verify_data) {
    if (req.op == Op::kWrite) {
      datapath_rcvr_->ClearData();
    } else {
      datapath_sndr_->ClearData();
    }
  }

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
        Metrics::IncrementTransfers(Component::kSenderDatapath,
                                    datapath_sndr_->DataSize());
        Metrics::IncrementTransfers(Component::kReceiverDatapath,
                                    datapath_sndr_->DataSize());
        if (flags_.verify_data) {
          CHECK(datapath_sndr_->Data() == datapath_rcvr_->Data())
              << "Data integrity verification failed!";
        }
      }
      break;
    }

    absl::SleepFor(absl::Microseconds(50));
  }
}

}  // namespace peregrine::integration
