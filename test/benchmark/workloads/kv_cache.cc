#include "test/benchmark/workloads/kv_cache.h"

#include <sys/resource.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
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

ABSL_FLAG(uint32_t, num_layers, 32,
          "Number of layers for kv_cache workload (default = 32)");

ABSL_FLAG(uint32_t, num_blocks, 64,
          "Number of blocks per layer for kv_cache workload (default = 64)");

ABSL_FLAG(uint64_t, block_size, 1024 * 1024ULL,
          "Block size in bytes for kv_cache workload (default = 1 MiB)");

namespace peregrine::benchmark {
namespace {
using ::peregrine::Byte;
using ::peregrine::Handle;
using ::peregrine::IsCompleted;
using ::peregrine::Op;
using ::peregrine::Request;
using ::peregrine::Status;
using ::peregrine::util::RandomNonZero;

absl::Duration GetCpuTime() {
  struct rusage ru;
  CHECK_EQ(getrusage(RUSAGE_SELF, &ru), 0);
  return absl::DurationFromTimeval(ru.ru_utime) +
         absl::DurationFromTimeval(ru.ru_stime);
}
}  // namespace

KvCache::KvCache(Transport* transport, int app_control_fd,
                 std::string_view server_endpoint, uint32_t num_layers,
                 uint32_t num_blocks, uint64_t block_size, uint64_t xfer_size,
                 uint32_t num_xfers)
    : transport_(transport),
      app_control_fd_(app_control_fd),
      server_endpoint_(server_endpoint),
      num_layers_(num_layers),
      num_blocks_(num_blocks),
      block_size_(block_size),
      xfer_size_(xfer_size),
      num_xfers_(num_xfers) {
  CHECK(transport_ != nullptr);
}

uint64_t KvCache::XferSize() {
  const uint32_t num_layers = std::max(1U, absl::GetFlag(FLAGS_num_layers));
  const uint32_t num_blocks = std::max(1U, absl::GetFlag(FLAGS_num_blocks));
  const uint64_t block_size =
      std::max(static_cast<uint64_t>(1), absl::GetFlag(FLAGS_block_size));
  return static_cast<uint64_t>(num_layers) * num_blocks * block_size;
}

std::unique_ptr<KvCache> KvCache::Create(Transport* transport,
                                         int app_control_fd,
                                         std::string_view server_endpoint) {
  const uint32_t num_layers = absl::GetFlag(FLAGS_num_layers);
  const uint32_t num_blocks = absl::GetFlag(FLAGS_num_blocks);
  const uint64_t block_size = absl::GetFlag(FLAGS_block_size);
  const uint64_t xfer_size =
      static_cast<uint64_t>(num_layers) * num_blocks * block_size;
  const uint32_t num_xfers = ParseNumXfers();

  QCHECK_GT(num_layers, 0) << "--num_layers must be greater than 0";
  QCHECK_GT(num_blocks, 0) << "--num_blocks must be greater than 0";
  QCHECK_GT(block_size, 0) << "--block_size must be greater than 0";
  QCHECK_GT(xfer_size, 0) << "Calculated xfer_size must be greater than 0";
  QCHECK_GT(num_xfers, 0) << "--num_xfers must be greater than 0";

  return std::make_unique<KvCache>(transport, app_control_fd, server_endpoint,
                                   num_layers, num_blocks, block_size,
                                   xfer_size, num_xfers);
}

absl::Status KvCache::Run() {
  const size_t total_blocks = static_cast<size_t>(num_layers_) * num_blocks_;

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
      "Workload    : kv_cache\n"
      "Total size  : %s (%d bytes)\n"
      "Num layers  : %d\n"
      "Num blocks  : %d (per layer)\n"
      "Total blocks: %d\n"
      "Block size  : %s (%d bytes)\n"
      "Num xfers   : %d\n"
      "Buffer addr : %p\n"
      "Buffer hash : 0x%x\n"
      "Sending to  : %s\n",
      ToString(xfer_size_), xfer_size_, num_layers_, num_blocks_, total_blocks,
      ToString(block_size_), block_size_, num_xfers_, buf.data(),
      util::Xx3Hash(buf), server_endpoint_);

  uint64_t total_bytes = 0;
  absl::Duration total_dur = absl::ZeroDuration();
  absl::Duration total_cpu_dur = absl::ZeroDuration();
  std::vector<double> latencies_ms;
  latencies_ms.reserve(num_xfers_);
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

    Byte* const remote_base =
        reinterpret_cast<Byte*>(response.transfer_response().buffer_address());

    // Prepare batch of transfer requests across all blocks.
    std::vector<Request> requests;
    requests.reserve(total_blocks);
    for (size_t b = 0; b < total_blocks; ++b) {
      const size_t offset = b * static_cast<size_t>(block_size_);
      const size_t len = std::min(static_cast<size_t>(block_size_),
                                  static_cast<size_t>(xfer_size_) - offset);
      requests.push_back({
          .op = Op::kWrite,
          .laddr = buf.data() + offset,
          .raddr = remote_base + offset,
          .len = len,
      });
    }

    const absl::Time start_time = absl::Now();
    const absl::Duration start_cpu = GetCpuTime();

    const absl::StatusOr<Handle> handle_or =
        transport_->Post(server_endpoint_, requests);
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
    latencies_ms.push_back(absl::ToDoubleMilliseconds(dur));
    total_bytes += xfer_size_;
    total_dur += dur;
    total_cpu_dur += cpu_dur;
    std::cout << absl::StrFormat(
        "Transfer %d/%d size: %s, latency: %s, thruput: %s, CPU time: %s\n", i,
        num_xfers_, ToString(xfer_size_), absl::FormatDuration(dur),
        ToString(CalcRate(xfer_size_, dur)), absl::FormatDuration(cpu_dur));
  }

  // Show summary.
  std::sort(latencies_ms.begin(), latencies_ms.end());
  double sum_ms = 0;
  for (double val : latencies_ms) {
    sum_ms += val;
  }
  const double mean_ms = sum_ms / num_xfers_;
  const double p50 = latencies_ms[static_cast<size_t>(num_xfers_ * 0.50)];
  const double p90 = latencies_ms[static_cast<size_t>(num_xfers_ * 0.90)];
  const double p99 = latencies_ms[static_cast<size_t>(num_xfers_ * 0.99)];
  const double throughput_gbs =
      (static_cast<double>(xfer_size_) / 1e9) / (mean_ms / 1000.0);

  std::cout << "\n### KV Cache Benchmark Results ###\n";
  std::cout << absl::StrFormat("Total Size:   %s (%d bytes)\n",
                               ToString(xfer_size_), xfer_size_);
  std::cout << absl::StrFormat("Num Layers:   %d\n", num_layers_);
  std::cout << absl::StrFormat("Num Blocks:   %d (per layer)\n", num_blocks_);
  std::cout << absl::StrFormat("Total Blocks: %d\n", total_blocks);
  std::cout << absl::StrFormat("Block Size:   %s (%d bytes)\n",
                               ToString(block_size_), block_size_);
  std::cout << absl::StrFormat("Transfers:    %d\n", num_xfers_);
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

}  // namespace peregrine::benchmark
