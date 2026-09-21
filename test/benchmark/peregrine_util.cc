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
#include "absl/status/status.h"
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
#include "test/benchmark/flags.h"
#include "test/benchmark/types.h"
#include "test/benchmark/workloads/workload_generator.h"
#include "test/benchmark/workloads/workload_util.h"

namespace peregrine::benchmark {

namespace {
using ::peregrine::Byte;
using ::peregrine::CreateTransport;
using ::peregrine::Handle;
using ::peregrine::IsCompleted;
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

absl::Status RunBenchmark(Transport* transport, int app_control_fd,
                          std::string_view server_endpoint,
                          const WorkloadGenerator& workload,
                          uint32_t num_xfers) {
  CHECK(transport != nullptr);
  CHECK_GT(num_xfers, 0);
  const uint64_t xfer_size = workload.TotalSizeBytes();

  // Pre-allocate source buffer.
  std::vector<Byte> buf(xfer_size);
  RandomNonZero(absl::MakeSpan(buf));
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b != 0; }));

  // Register memory buffer for RDMA if required.
  const absl::Status reg_status =
      transport->RegisterMemory(buf.data(), buf.size());
  if (!reg_status.ok()) {
    return reg_status;
  }

  // Show workload info.
  std::cout << absl::StrFormat(
      "Workload    : %s\n"
      "Num xfers   : %d\n"
      "Buffer addr : %p\n"
      "Buffer hash : 0x%x\n"
      "Sending to  : %s\n",
      workload.Name(), num_xfers, buf.data(), util::Xx3Hash(buf),
      server_endpoint);

  absl::Duration total_dur = absl::ZeroDuration();
  absl::Duration total_cpu_dur = absl::ZeroDuration();
  std::vector<double> latencies_ms;
  latencies_ms.reserve(num_xfers);
  proto::ControlMessage request;
  proto::ControlMessage response;

  for (uint32_t i = 1; i <= num_xfers; ++i) {
    // Request remote destination address.
    request.Clear();
    request.mutable_transfer_request();
    if (!SendControlMessage(app_control_fd, request)) {
      return absl::InternalError(
          absl::StrFormat("Failed to send TransferRequest at transfer %d", i));
    }

    // Receive destination address.
    response.Clear();
    if (!ProcessControlMessage(app_control_fd, &response)) {
      return absl::InternalError(absl::StrFormat(
          "Failed to receive TransferResponse at transfer %d", i));
    }
    if (!response.has_transfer_response()) {
      return absl::InternalError(
          absl::StrFormat("Missing transfer_response at transfer %d", i));
    }

    Byte* const base_raddr =
        reinterpret_cast<Byte*>(response.transfer_response().buffer_address());

    // Generate requests from workload layer.
    const std::vector<Request> requests =
        workload.GenerateRequests(buf.data(), base_raddr);

    const absl::Time start_time = absl::Now();
    const absl::Duration start_cpu = GetCpuTime();

    const absl::StatusOr<Handle> handle_or =
        transport->Post(server_endpoint, requests);
    if (!handle_or.ok()) {
      return handle_or.status();
    }

    // Poll for status.
    const Handle handle = handle_or.value();
    while (true) {
      const absl::StatusOr<Status> s = transport->Poll(handle);
      if (!s.ok()) {
        return s.status();
      } else if (const Status status = s.value(); !IsCompleted(status)) {
        absl::SleepFor(absl::Microseconds(100));
      } else {
        if (status != Status::kSuccess) {
          return absl::InternalError(absl::StrFormat(
              "Transfer %d failed with status %s", i, ToString(status)));
        }
        break;
      }
    }

    // Measure the transfer.
    const absl::Duration dur = absl::Now() - start_time;
    const absl::Duration cpu_dur = GetCpuTime() - start_cpu;
    latencies_ms.push_back(absl::ToDoubleMilliseconds(dur));
    total_dur += dur;
    total_cpu_dur += cpu_dur;
    std::cout << absl::StrFormat(
        "Transfer %d/%d size: %s, latency: %s, thruput: %s, CPU time: %s\n", i,
        num_xfers, ToString(xfer_size), absl::FormatDuration(dur),
        ToString(CalcRate(xfer_size, dur)), absl::FormatDuration(cpu_dur));
  }

  // Show summary statistics.
  std::sort(latencies_ms.begin(), latencies_ms.end());
  double sum_ms = 0;
  for (double val : latencies_ms) {
    sum_ms += val;
  }
  const double mean_ms = sum_ms / num_xfers;
  const double p50 = latencies_ms[static_cast<size_t>(num_xfers * 0.50)];
  const double p90 = latencies_ms[static_cast<size_t>(num_xfers * 0.90)];
  const double p99 = latencies_ms[static_cast<size_t>(num_xfers * 0.99)];
  const double throughput_gbs =
      (static_cast<double>(xfer_size) / 1e9) / (mean_ms / 1000.0);

  std::cout << absl::StrFormat("\n### %s Benchmark Results ###\n",
                               workload.Name());
  std::cout << absl::StrFormat("Total Size:   %s (%u bytes)\n",
                               ToString(xfer_size), xfer_size);
  std::cout << absl::StrFormat("Transfers:    %u\n", num_xfers);
  std::cout << absl::StrFormat("p50:          %8.3f ms\n", p50);
  std::cout << absl::StrFormat("p90:          %8.3f ms\n", p90);
  std::cout << absl::StrFormat("p99:          %8.3f ms\n", p99);
  std::cout << absl::StrFormat("Mean:         %8.3f ms\n", mean_ms);
  std::cout << absl::StrFormat("Throughput:   %8.3f GB/s (%.2f Gbps)\n",
                               throughput_gbs, throughput_gbs * 8.0);
  std::cout << absl::StrFormat(
      "CPU usage:    %.2f cores\n",
      total_dur > absl::ZeroDuration()
          ? absl::FDivDuration(total_cpu_dur, total_dur)
          : 0.0);

  return absl::OkStatus();
}

void RunClient(std::string_view ip, uint16_t peregrine_control_port,
               uint16_t app_control_port, int nconns,
               std::string_view peer_host, TransportType transport_type,
               WorkloadType workload) {
  // Connect to server control.
  const int app_control_fd =
      ConnectControlWithRetry(peer_host, app_control_port);

  // Create transport.
  const std::string self = GenEndpoint(ip, /*port=*/0);
  const std::unique_ptr<Transport> transport =
      CreateTransport(self, transport_type, nconns);
  CHECK(transport != nullptr) << "Failed to create transport";

  const std::string server_endpoint =
      GenEndpoint(peer_host, peregrine_control_port);

  // Show info.
  std::cout << absl::StrFormat(
      "Role: client\n"
      "Connections : %d\n"
      "Listening at: %s\n"
      "Sending to  : %s\n",
      nconns, self, server_endpoint);

  const uint32_t num_xfers = ParseNumXfers();
  QCHECK_GT(num_xfers, 0) << "--num_xfers must be greater than 0";

  // Create workload and run benchmark engine.
  std::unique_ptr<WorkloadGenerator> generator = CreateWorkload(workload);
  CHECK(generator != nullptr)
      << "Failed to create workload generator for workload: "
      << ToString(workload);
  CHECK_OK(RunBenchmark(transport.get(), app_control_fd, server_endpoint,
                        *generator, num_xfers));

  close(app_control_fd);
}

}  // namespace peregrine::benchmark
