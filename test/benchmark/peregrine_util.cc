#include "test/benchmark/peregrine_util.h"

#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"
#include "test/benchmark/control.pb.h"
#include "test/benchmark/control_util.h"
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
using ::peregrine::util::FindFreePort;
using ::peregrine::util::RandomNonZero;

std::string GenEndpoint(std::string_view ip, uint16_t port) {
  const bool ipv6 = absl::StrContains(ip, ':');
  const int family = ipv6 ? AF_INET6 : AF_INET;
  const uint16_t listen_port = port ?: FindFreePort(family, /*tcp=*/true);
  CHECK_GT(listen_port, 0);
  return ipv6 ? absl::StrCat("[", ip, "]:", listen_port)
              : absl::StrCat(ip, ":", listen_port);
}

absl::Duration GetCpuTime() {
  struct rusage ru;
  CHECK_EQ(getrusage(RUSAGE_SELF, &ru), 0);
  return absl::DurationFromTimeval(ru.ru_utime) +
         absl::DurationFromTimeval(ru.ru_stime);
}
}  // namespace

void RunServer(std::string_view ip, uint16_t peregrine_control_port,
               uint16_t app_control_port, int nconns, uint64_t xfer_size,
               TransportType transport_type) {
  // Create transport.
  const std::string self = GenEndpoint(ip, peregrine_control_port);
  const std::unique_ptr<Transport> transport =
      CreateTransport(self, transport_type, nconns);
  CHECK(transport != nullptr) << "Failed to create transport";

  // Wait for client's control connection.
  const int control_fd = CreateControlListener(ip, app_control_port);
  const int client_fd = accept(control_fd, nullptr, nullptr);
  CHECK_GE(client_fd, 0) << "Failed to accept control client connection";

  // Allocate buffer and fill with zeros.
  std::vector<Byte> buf(xfer_size, 0);
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b == 0; }));

  // Register memory buffer for RDMA if required.
  CHECK_OK(transport->RegisterMemory(buf.data(), buf.size()));

  // Show info.
  std::cout << absl::StrFormat(
      "Role: server\n"
      "Buffer addr : %p\n"
      "Buffer size : %s\n"
      "Listening at: %s\n"
      "App control : %d\n"
      "Control negotiated. Press Ctrl+C to terminate.",
      buf.data(), ToString(buf.size()), self, app_control_port);

  const absl::Time start_time = absl::Now();
  const absl::Duration start_cpu = GetCpuTime();

  proto::ControlMessage response;
  // Process transfer requests until client disconnects.
  while (true) {
    proto::ControlMessage request;
    // Wait for client to send TransferRequest.
    if (!ProcessControlMessage(client_fd, &request)) {
      break;
    }
    CHECK(request.has_transfer_request()) << "Expected TransferRequest";

    // Send TransferResponse with buffer address.
    response.Clear();
    auto* transfer_response = response.mutable_transfer_response();
    transfer_response->set_buffer_address(
        reinterpret_cast<uint64_t>(buf.data()));
    CHECK(SendControlMessage(client_fd, response))
        << "Failed to send TransferResponse";
  }

  const absl::Duration dur = absl::Now() - start_time;
  const absl::Duration cpu_dur = GetCpuTime() - start_cpu;

  std::cout << absl::StrFormat(
      "\nServer completed resolving all transfers.\n"
      "Total time    : %s\n"
      "Total CPU time: %s\n"
      "CPU usage     : %.2f cores\n",
      absl::FormatDuration(dur), absl::FormatDuration(cpu_dur),
      dur > absl::ZeroDuration() ? absl::FDivDuration(cpu_dur, dur) : 0.0);

  close(client_fd);
  close(control_fd);
}

void RunClient(std::string_view ip, uint16_t peregrine_control_port,
               uint16_t app_control_port, int nconns, uint64_t xfer_size,
               std::string_view peer_host, uint32_t num_xfers,
               TransportType transport_type) {
  // Connect to server control.
  const int server_fd = ConnectControlWithRetry(peer_host, app_control_port);

  // Pre-allocate source buffer.
  std::vector<Byte> buf(xfer_size);
  RandomNonZero(absl::MakeSpan(buf));
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b != 0; }));

  // Create transport.
  const std::string self = GenEndpoint(ip, /*port=*/0);
  const std::unique_ptr<Transport> transport =
      CreateTransport(self, transport_type, nconns);
  CHECK(transport != nullptr) << "Failed to create transport";

  // Register memory buffer for RDMA if required.
  CHECK_OK(transport->RegisterMemory(buf.data(), buf.size()));

  const std::string peer_endpoint =
      GenEndpoint(peer_host, peregrine_control_port);

  // Show info.
  std::cout << absl::StrFormat(
      "Role: client\n"
      "Connections : %d\n"
      "Buffer addr : %p\n"
      "Buffer size : %s\n"
      "Buffer hash : 0x%x\n"
      "Listening at: %s\n"
      "Sending to  : %s\n",
      nconns, buf.data(), ToString(buf.size()), util::Xx3Hash(buf), self,
      peer_endpoint);

  uint64_t total_bytes = 0;
  absl::Duration total_dur = absl::ZeroDuration();
  absl::Duration total_cpu_dur = absl::ZeroDuration();
  for (uint32_t i = 1; i <= num_xfers; ++i) {
    // Request remote destination address.
    proto::ControlMessage request;
    request.mutable_transfer_request();
    CHECK(SendControlMessage(server_fd, request))
        << "Failed to send TransferRequest at transfer " << i;

    // Receive destination address.
    proto::ControlMessage response;
    CHECK(ProcessControlMessage(server_fd, &response))
        << "Failed to receive TransferResponse at transfer " << i;
    CHECK(response.has_transfer_response());

    // Post standard write request using dynamically resolved buffer address.
    const Request req = {
        .op = Op::kWrite,
        .laddr = buf.data(),
        .raddr = reinterpret_cast<Byte*>(
            response.transfer_response().buffer_address()),
        .len = static_cast<size_t>(xfer_size),
    };
    const absl::Time start_time = absl::Now();
    const absl::Duration start_cpu = GetCpuTime();
    const absl::StatusOr<Handle> handle_or =
        transport->Post(peer_endpoint, {req});
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
    const absl::Duration cpu_dur = GetCpuTime() - start_cpu;
    total_bytes += xfer_size;
    total_dur += dur;
    total_cpu_dur += cpu_dur;
    std::cout << absl::StrFormat(
        "Transfer %d/%d size: %s, latency: %s, thruput: %s, CPU time: %s\n", i,
        num_xfers, ToString(xfer_size), absl::FormatDuration(dur),
        ToString(CalcRate(xfer_size, dur)), absl::FormatDuration(cpu_dur));
  }

  // Show summary.
  std::cout << absl::StrFormat(
      "--- Summary ---\n"
      "Total bytes   : %s\n"
      "Total time    : %s\n"
      "Total CPU time: %s\n"
      "Avg latency   : %s\n"
      "Avg thruput   : %s\n"
      "CPU usage     : %.2f cores\n",
      ToString(total_bytes), absl::FormatDuration(total_dur),
      absl::FormatDuration(total_cpu_dur),
      absl::FormatDuration(total_dur / num_xfers),
      ToString(CalcRate(total_bytes, total_dur)),
      total_dur > absl::ZeroDuration()
          ? absl::FDivDuration(total_cpu_dur, total_dur)
          : 0.0);

  close(server_fd);
}

}  // namespace peregrine::benchmark
