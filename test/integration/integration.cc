#include "test/integration/integration.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
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
#include "test/integration/workloads/batch-generator.h"
#include "test/integration/workloads/serial-fixed-write.h"

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

  batch_generator_ = std::make_unique<SerialFixedWrite>(
      controlpath_rcvr_->endpoint(), datapath_sndr_->DataPtr(),
      datapath_rcvr_->DataPtr(), datapath_sndr_->DataSize());
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
    runBatch();
  }
}

void PeregrineIntegration::runBatch() {
  // 1. Get a Post batch.
  PostBatch batch = batch_generator_->NextBatch();
  if (batch.items.empty()) {
    return;
  }

  if (flags_.verify_data) {
    datapath_rcvr_->ClearData();
  }

  // 2. Call Post for each PostItem.
  PendingHandles pending = postItems(absl::MakeSpan(batch.items));

  // 3. Sleep.
  if (batch.sleep > absl::ZeroDuration()) {
    absl::SleepFor(batch.sleep);
  }

  // 4. Poll all handles (if its on_complete == nullptr), wait until the batch
  // is done.
  pollHandles(pending);
}

PeregrineIntegration::PendingHandles PeregrineIntegration::postItems(
    absl::Span<PostItem> items) {
  PendingHandles pending;
  pending.reserve(items.size());

  for (PostItem& item : items) {
    const bool need_poll = (item.on_complete == nullptr);
    int64_t item_bytes = 0;
    for (const Request& req : item.requests) {
      item_bytes += req.len;
    }

    absl::AnyInvocable<void(Status)> on_complete = nullptr;
    if (!need_poll) {
      on_complete = [this, item_bytes,
                     cb = std::move(item.on_complete)](Status s) mutable {
        if (s == Status::kSuccess) {
          onTransferSuccess(item_bytes);
        }
        cb(s);
      };
    }

    const absl::StatusOr<Handle> handle_or = datapath_sndr_->Post(
        item.peer, item.requests, std::move(on_complete));
    if (!handle_or.ok()) {
      // TODO(yyd): Add an app-level metric for this.
      continue;
    }
    if (need_poll) {
      pending.push_back({.handle = handle_or.value(), .bytes = item_bytes});
    }
  }
  return pending;
}

void PeregrineIntegration::pollHandles(PendingHandles& pending) {
  while (shouldContinue() && !pending.empty()) {
    for (auto it = pending.begin(); it != pending.end();) {
      const auto status_or = datapath_sndr_->Poll(it->handle);
      if (!status_or.ok()) {
        it = pending.erase(it);
        continue;
      }

      const Status s = status_or.value();
      if (IsCompleted(s)) {
        if (s == Status::kSuccess) {
          onTransferSuccess(it->bytes);
        }
        it = pending.erase(it);
      } else {
        ++it;
      }
    }
    if (!pending.empty()) {
      absl::SleepFor(absl::Microseconds(10));
    }
  }
}

void PeregrineIntegration::onTransferSuccess(int64_t bytes) {
  Metrics::IncrementTransfers(Component::kSenderDatapath, bytes);
  Metrics::IncrementTransfers(Component::kReceiverDatapath, bytes);
  if (flags_.verify_data) {
    CHECK(datapath_sndr_->Data() == datapath_rcvr_->Data())
        << "Data integrity verification failed!";
  }
}

}  // namespace peregrine::integration
