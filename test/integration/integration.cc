#include "test/integration/integration.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/util/app.h"
#include "test/integration/flags.h"

namespace peregrine::integration {

PeregrineIntegration::PeregrineIntegration() {
  constexpr size_t kBufSize = 1024;
  sender_ = std::make_unique<util::App>(kBufSize, /*num_conns_per_peer=*/1);
  receiver_ = std::make_unique<util::App>(kBufSize, /*num_conns_per_peer=*/1);

  settings_.test_begin = absl::Now();
  settings_.test_duration =
      std::max(absl::Seconds(1), absl::GetFlag(FLAGS_test_duration));
}

PeregrineIntegration::~PeregrineIntegration() = default;

void PeregrineIntegration::Run() {
  sender_->GenData();
  receiver_->ClearData();

  // Wait for the test to complete.
  Wait();
}

bool PeregrineIntegration::Continue() {
  return !stop_.load() &&
         absl::Now() < settings_.test_begin + settings_.test_duration;
}

void PeregrineIntegration::Wait() {
  while (Continue()) {
    SendRequest();

    absl::SleepFor(absl::Milliseconds(10));  // Simulate delay between transfers
  }
}

void PeregrineIntegration::SendRequest() {
  Transport& sender_transport = sender_->GetTransport();
  const std::string peer = receiver_->GetEndpoint();

  Request req = {
      .op = Op::kWrite,
      .laddr = sender_->DataPtr(),
      .raddr = receiver_->DataPtr(),
      .len = sender_->DataSize(),
  };

  auto handle_or = sender_transport.Post(peer, {req});
  if (handle_or.ok()) {
    Handle h = handle_or.value();
    // Poll for completion
    while (Continue()) {
      auto status_or = sender_transport.Poll(h);
      if (!status_or.ok()) {
        break;
      }

      const Status s = status_or.value();
      if (IsCompleted(s)) {
        if (s == Status::kSuccess) {
          n_transfers_.fetch_add(1);
          n_bytes_.fetch_add(sender_->DataSize());
        }
        break;
      }
      absl::SleepFor(absl::Microseconds(100));
    }
  }
}

void PeregrineIntegration::Stop() {
  stop_.store(true);
}

}  // namespace peregrine::integration
