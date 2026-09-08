#include "test/benchmark/workloads/serial_fixed_write.h"

#include <sys/resource.h>

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
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/util/util.h"
#include "test/benchmark/control.pb.h"
#include "test/benchmark/control_util.h"
#include "test/benchmark/flags.h"
#include "test/benchmark/types.h"

namespace peregrine::benchmark {
namespace {
using ::peregrine::Byte;
using ::peregrine::util::RandomNonZero;

absl::Duration GetCpuTime() {
  struct rusage ru;
  CHECK_EQ(getrusage(RUSAGE_SELF, &ru), 0);
  return absl::DurationFromTimeval(ru.ru_utime) +
         absl::DurationFromTimeval(ru.ru_stime);
}
}  // namespace

SerialFixedWrite::SerialFixedWrite(Transport* transport, int app_control_fd,
                                   std::string_view server_endpoint,
                                   uint64_t xfer_size, uint32_t num_xfers)
    : transport_(transport),
      app_control_fd_(app_control_fd),
      server_endpoint_(server_endpoint),
      xfer_size_(xfer_size),
      num_xfers_(num_xfers) {
  CHECK(transport_ != nullptr);
}

std::unique_ptr<SerialFixedWrite> SerialFixedWrite::Create(
    Transport* transport, int app_control_fd,
    std::string_view server_endpoint) {
  const uint64_t xfer_size = ParseXferSize();
  const uint32_t num_xfers = ParseNumXfers();
  QCHECK_GT(xfer_size, 0) << "--xfer_size must be greater than 0";
  QCHECK_GT(num_xfers, 0) << "--num_xfers must be greater than 0";
  return std::make_unique<SerialFixedWrite>(
      transport, app_control_fd, server_endpoint, xfer_size, num_xfers);
}

absl::Status SerialFixedWrite::Run() {
  // Pre-allocate source buffer.
  std::vector<Byte> buf(xfer_size_);
  RandomNonZero(absl::MakeSpan(buf));
  DCHECK(std::all_of(buf.begin(), buf.end(), [](Byte b) { return b != 0; }));

  // Register memory buffer for RDMA if required.
  const absl::Status reg_status =
      transport_->RegisterMemory(buf.data(), buf.size());
  if (!reg_status.ok()) {
    return reg_status;
  }

  // Show info.
  std::cout << absl::StrFormat(
      "Workload    : serial_fixed_write\n"
      "Buffer addr : %p\n"
      "Buffer size : %s\n"
      "Buffer hash : 0x%x\n"
      "Sending to  : %s\n",
      buf.data(), ToString(buf.size()), util::Xx3Hash(buf), server_endpoint_);

  uint64_t total_bytes = 0;
  absl::Duration total_dur = absl::ZeroDuration();
  absl::Duration total_cpu_dur = absl::ZeroDuration();
  proto::ControlMessage request;
  proto::ControlMessage response;
  for (uint32_t i = 1; i <= num_xfers_; ++i) {
    // Request remote destination address.
    request.Clear();
    request.mutable_transfer_request();
    if (!SendControlMessage(app_control_fd_, request)) {
      return absl::InternalError(
          absl::StrFormat("Failed to send TransferRequest at transfer %d", i));
    }

    // Receive destination address.
    response.Clear();
    if (!ProcessControlMessage(app_control_fd_, &response)) {
      return absl::InternalError(absl::StrFormat(
          "Failed to receive TransferResponse at transfer %d", i));
    }
    if (!response.has_transfer_response()) {
      return absl::InternalError(
          absl::StrFormat("Missing transfer_response at transfer %d", i));
    }

    // Post standard write request using dynamically resolved buffer address.
    const Request req = {
        .op = Op::kWrite,
        .laddr = buf.data(),
        .raddr = reinterpret_cast<Byte*>(
            response.transfer_response().buffer_address()),
        .len = static_cast<size_t>(xfer_size_),
    };
    const absl::Time start_time = absl::Now();
    const absl::Duration start_cpu = GetCpuTime();
    const absl::StatusOr<Handle> handle_or =
        transport_->Post(server_endpoint_, {req});
    if (!handle_or.ok()) {
      return handle_or.status();
    }

    // Poll for status.
    const Handle handle = handle_or.value();
    while (true) {
      const absl::StatusOr<Status> s = transport_->Poll(handle);
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
    total_bytes += xfer_size_;
    total_dur += dur;
    total_cpu_dur += cpu_dur;
    std::cout << absl::StrFormat(
        "Transfer %d/%d size: %s, latency: %s, thruput: %s, CPU time: %s\n", i,
        num_xfers_, ToString(xfer_size_), absl::FormatDuration(dur),
        ToString(CalcRate(xfer_size_, dur)), absl::FormatDuration(cpu_dur));
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
      absl::FormatDuration(total_dur / num_xfers_),
      ToString(CalcRate(total_bytes, total_dur)),
      total_dur > absl::ZeroDuration()
          ? absl::FDivDuration(total_cpu_dur, total_dur)
          : 0.0);

  return absl::OkStatus();
}

}  // namespace peregrine::benchmark
