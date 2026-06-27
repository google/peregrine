#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"

ABSL_FLAG(std::string, role, "receiver",
          "Role to run as: 'sender' or 'receiver'");
ABSL_FLAG(std::string, peer, "",
          "Receiver's endpoint ip:port (required for sender)");
ABSL_FLAG(uint16_t, port, 0, "Port to listen on (0 for unused/free port)");
ABSL_FLAG(uint64_t, transfer_size, 1024ULL * 1024 * 1024,
          "Buffer transfer size in bytes (must be a multiple of 2, "
          "default 1GB)");
ABSL_FLAG(int, num_transfers, 1, "Number of transfers to perform (default 1)");
ABSL_FLAG(std::string, raddr, "",
          "Remote buffer address in hex or decimal (required for sender)");

namespace {

using ::peregrine::Byte;
using ::peregrine::Handle;
using ::peregrine::Op;
using ::peregrine::Request;
using ::peregrine::Status;

// Parses remote buffer memory address from the flag.
// Returns nullptr if invalid.
Byte* ParseRemoteAddress(std::string_view raddr_str) {
  uint64_t val = 0;
  if (raddr_str.starts_with("0x") || raddr_str.starts_with("0X")) {
    raddr_str.remove_prefix(2);
    if (!absl::SimpleHexAtoi(raddr_str, &val)) return nullptr;
  } else {
    if (!absl::SimpleAtoi(raddr_str, &val)) return nullptr;
  }
  return reinterpret_cast<Byte*>(val);
}

// Validates command line flags for both sender and receiver roles.
void ValidateFlags() {
  const std::string role = absl::GetFlag(FLAGS_role);
  CHECK(role == "sender" || role == "receiver")
      << "Flag --role must be 'sender' or 'receiver'";

  const uint64_t transfer_size = absl::GetFlag(FLAGS_transfer_size);
  CHECK_GT(transfer_size, 0) << "Flag --transfer_size must be greater than 0";
  CHECK_EQ(transfer_size % 2, 0)
      << "Flag --transfer_size must be a multiple of 2";

  if (role == "sender") {
    CHECK(!absl::GetFlag(FLAGS_peer).empty())
        << "Flag --peer must be specified for sender";

    const std::string raddr_str = absl::GetFlag(FLAGS_raddr);
    CHECK(!raddr_str.empty()) << "Flag --raddr must be specified for sender";
    CHECK(ParseRemoteAddress(raddr_str) != nullptr)
        << "Failed to parse remote address --raddr: " << raddr_str;

    CHECK_GT(absl::GetFlag(FLAGS_num_transfers), 0)
        << "Flag --num_transfers must be greater than 0";
  }
}

void RunReceiver(uint16_t port, uint64_t transfer_size) {
  // Allocate buffer of size N
  auto buffer = std::make_unique<Byte[]>(transfer_size);
  std::cout << "Role: receiver" << std::endl;
  std::cout << "Buffer address: " << static_cast<void*>(buffer.get())
            << std::endl;

  // Determine port
  uint16_t listen_port = port;
  if (listen_port == 0) {
    listen_port = ::peregrine::util::FindFreePort(AF_INET, /*tcp=*/true);
    CHECK_GT(listen_port, 0) << "Failed to find a free port";
  }

  std::string endpoint = absl::StrCat("0.0.0.0:", listen_port);
  std::cout << "Listening endpoint: " << endpoint << std::endl;

  std::unique_ptr<peregrine::Transport> transport =
      peregrine::CreateTransport(endpoint);
  CHECK(transport != nullptr) << "Failed to create transport";

  std::cout << "Receiver is listening. Press Ctrl+C to terminate." << std::endl;
  while (true) {
    absl::SleepFor(absl::Seconds(1));
  }
}

void RunSender(std::string_view peer, uint16_t self_port, Byte* raddr,
               uint64_t transfer_size, int num_transfers) {
  // Allocate buffer of size N
  auto buffer = std::make_unique<Byte[]>(transfer_size);
  std::cout << "Role: sender" << std::endl;
  std::cout << "Buffer address: " << static_cast<void*>(buffer.get())
            << std::endl;

  // Fill buffer with some non-zero data
  std::fill(buffer.get(), buffer.get() + transfer_size, 0xAB);

  // Create transport for sender
  uint16_t listen_port = self_port;
  if (listen_port == 0) {
    listen_port = ::peregrine::util::FindFreePort(AF_INET, /*tcp=*/true);
    CHECK_GT(listen_port, 0) << "Failed to find a free port for sender";
  }
  std::string endpoint = absl::StrCat("0.0.0.0:", listen_port);
  std::unique_ptr<peregrine::Transport> transport =
      peregrine::CreateTransport(endpoint);
  CHECK(transport != nullptr) << "Failed to create transport for sender";

  std::cout << "Sending to peer: " << peer << " at remote address "
            << static_cast<void*>(raddr) << std::endl;

  absl::Duration total_duration = absl::ZeroDuration();

  for (int i = 0; i < num_transfers; ++i) {
    absl::Time start_time = absl::Now();

    const Request req = {
        .op = Op::kWrite,
        .laddr = buffer.get(),
        .raddr = raddr,
        .len = static_cast<size_t>(transfer_size),
    };

    auto status_or_handle = transport->Post(peer, {req});
    if (!status_or_handle.ok()) {
      LOG(FATAL) << "Failed to post request: "
                 << status_or_handle.status().message();
    }
    Handle handle = status_or_handle.value();

    // Poll for completion
    while (true) {
      auto status_or_status = transport->Poll(handle);
      if (!status_or_status.ok()) {
        LOG(FATAL) << "Failed to poll status: "
                   << status_or_status.status().message();
      }
      Status s = status_or_status.value();
      if (peregrine::IsCompleted(s)) {
        CHECK_EQ(s, Status::kSuccess) << "Transfer failed";
        break;
      }
      absl::SleepFor(absl::Microseconds(1));
    }

    absl::Time end_time = absl::Now();
    absl::Duration duration = end_time - start_time;
    int64_t dur_us = absl::ToInt64Microseconds(duration);
    CHECK_GT(dur_us, 0);

    // Compute throughput in Gbps (1 Gbps = 10^9 bits)
    double size_gbits = (static_cast<double>(transfer_size) * 8.0) / 1e9;
    double throughput_gbps = size_gbits / static_cast<double>(dur_us) * 1e6;

    std::cout << "Transfer " << i + 1 << "/" << num_transfers
              << ": Latency = " << static_cast<double>(dur_us) / 1e3
              << " ms, Throughput = " << throughput_gbps << " Gbps"
              << std::endl;

    total_duration += duration;
  }

  double total_duration_sec = absl::ToDoubleSeconds(total_duration);
  double total_gbits =
      (static_cast<double>(transfer_size) * 8.0 * num_transfers) / 1e9;
  double avg_throughput_gbps =
      (total_duration_sec > 0) ? (total_gbits / total_duration_sec) : 0;
  double avg_latency_ms =
      (absl::ToDoubleMilliseconds(total_duration) / num_transfers);

  std::cout << "--- Summary ---" << std::endl;
  std::cout << "Average Latency: " << avg_latency_ms << " ms" << std::endl;
  std::cout << "Average Throughput: " << avg_throughput_gbps << " Gbps"
            << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Validate command line flags
  ValidateFlags();

  std::string role = absl::GetFlag(FLAGS_role);
  uint16_t port = absl::GetFlag(FLAGS_port);
  uint64_t transfer_size = absl::GetFlag(FLAGS_transfer_size);
  int num_transfers = absl::GetFlag(FLAGS_num_transfers);
  std::string peer = absl::GetFlag(FLAGS_peer);

  if (role == "receiver") {
    RunReceiver(port, transfer_size);
  } else if (role == "sender") {
    Byte* raddr = ParseRemoteAddress(absl::GetFlag(FLAGS_raddr));
    RunSender(peer, port, raddr, transfer_size, num_transfers);
  }

  return 0;
}
