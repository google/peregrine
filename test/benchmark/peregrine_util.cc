#include "test/benchmark/peregrine_util.h"

#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"
#include "test/benchmark/types.h"

namespace peregrine::benchmark {

namespace {
using ::peregrine::Byte;
using ::peregrine::CreateTransport;
using ::peregrine::Handle;
using ::peregrine::IsCompleted;
using ::peregrine::Op;
using ::peregrine::Request;
using ::peregrine::Status;
using ::peregrine::Transport;
using ::peregrine::util::FindFreePort;
using ::peregrine::util::RandomNonZero;

std::string GenEndpoint(bool ipv4, uint16_t port) {
  const int family = ipv4 ? AF_INET : AF_INET6;
  const std::string_view ip = ipv4 ? "0.0.0.0" : "[::]";
  const uint16_t listen_port = port ?: FindFreePort(family, /*tcp=*/true);
  CHECK_GT(listen_port, 0);
  return absl::StrCat(ip, ":", listen_port);
}
}  // namespace

void RunRcvr(bool ipv4, uint16_t port, int nconns, uint64_t xfer_size) {
  // Allocate buffer and fill with zeros.
  const std::vector<Byte> buf(xfer_size, 0);
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b == 0; }));

  // Create transport.
  const std::string self = GenEndpoint(ipv4, port);
  const std::unique_ptr<Transport> transport = CreateTransport(self, nconns);
  CHECK(transport != nullptr) << "Failed to create transport";

  // Show info (which the sender needs to know).
  std::cout << absl::StrFormat(
      "Role: receiver\n"
      "Buffer addr : %p\n"
      "Buffer size : %s\n"
      "Listening at: %s\n"
      "Press Ctrl+C to terminate.",
      buf.data(), ToString(buf.size()), self);

  // Wait indefinitely.
  while (true) {
    absl::SleepFor(absl::Seconds(1));
  }
}

void RunSndr(bool ipv4, uint16_t port, int nconns, uint64_t xfer_size,
             std::string_view peer, void* raddr, uint32_t num_xfers) {
  // Allocate buffer and fill with non-zero data.
  std::vector<Byte> buf(xfer_size);
  RandomNonZero(absl::MakeSpan(buf));
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b != 0; }));

  // Create transport.
  const std::string self = GenEndpoint(ipv4, port);
  const std::unique_ptr<Transport> transport = CreateTransport(self, nconns);
  CHECK(transport != nullptr) << "Failed to create transport";

  // Show info.
  std::cout << absl::StrFormat(
      "Role: sender\n"
      "Connections : %d\n"
      "Buffer addr : %p\n"
      "Buffer size : %s\n"
      "Buffer hash : 0x%x\n"
      "Listening at: %s\n"
      "Sending to  : %s at raddr %p\n",
      nconns, buf.data(), ToString(buf.size()), util::Xx3Hash(buf), self, peer,
      raddr);

  uint64_t total_bytes = 0;
  absl::Duration total_dur = absl::ZeroDuration();
  for (int i = 1; i <= num_xfers; ++i) {
    const absl::Time start_time = absl::Now();

    // Post a request.
    const Request req = {
        .op = Op::kWrite,
        .laddr = buf.data(),
        .raddr = reinterpret_cast<Byte*>(raddr),
        .len = static_cast<size_t>(xfer_size),
    };
    const absl::StatusOr<Handle> handle_or = transport->Post(peer, {req});
    if (!handle_or.ok()) {
      LOG(FATAL) << "Failed to post request: " << handle_or.status().message();
    }

    // Poll for status.
    const Handle handle = handle_or.value();
    while (true) {
      const absl::StatusOr<Status> s = transport->Poll(handle);
      if (!s.ok()) {
        LOG(FATAL) << "Failed to poll status: " << s.status().message();
      } else if (const Status status = s.value(); !IsCompleted(status)) {
        absl::SleepFor(absl::Microseconds(100));
      } else {
        CHECK_EQ(status, Status::kSuccess) << "Transfer failed";
        break;
      }
    }

    // Measure the transfer.
    const absl::Duration dur = absl::Now() - start_time;
    total_bytes += xfer_size;
    total_dur += dur;
    std::cout << absl::StrFormat(
        "Transfer %d/%d size: %s, latency: %s, thruput: %s\n", i, num_xfers,
        ToString(xfer_size), absl::FormatDuration(dur),
        ToString(CalcRate(xfer_size, dur)));
  }

  // Show summary.
  std::cout << absl::StrFormat(
      "--- Summary ---\n"
      "Total bytes: %s\n"
      "Total time : %s\n"
      "Avg latency: %s\n"
      "Avg thruput: %s\n",
      ToString(total_bytes), absl::FormatDuration(total_dur),
      absl::FormatDuration(total_dur / num_xfers),
      ToString(CalcRate(total_bytes, total_dur)));
}

}  // namespace peregrine::benchmark
