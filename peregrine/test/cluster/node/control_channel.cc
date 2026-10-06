#include "peregrine/test/cluster/node/control_channel.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/metrics.h"
#include "peregrine/test/cluster/node/topology.h"

namespace peregrine::cluster {
namespace {

std::string FormatHistogramJson(const Log2Histogram<32>& h) {
  return absl::StrFormat(R"({"sum":%u,"count":%u,"buckets":[%s]})", h.sum,
                         h.Count(), absl::StrJoin(h.buckets, ","));
}

std::string FormatOpMetricsJson(const OpMetrics& op) {
  return absl::StrFormat(
      R"({"bytes":%u,"errors":%u,"e2e_latency_us":%s,"request_size_bytes":%s})",
      op.bytes, op.errors, FormatHistogramJson(op.e2e_latency_us),
      FormatHistogramJson(op.request_size_bytes));
}

std::string FormatCpuStatsJson(const CpuStats& cpu) {
  return absl::StrFormat(
      R"({"wall_ms":%.4f,"user_cpu_ms":%.4f,"sys_cpu_ms":%.4f,"avg_cores":%.4f})",
      cpu.wall_ms, cpu.user_cpu_ms, cpu.sys_cpu_ms, cpu.avg_cores);
}

std::string FormatTransportMetricsJson(const TransportMetrics& t) {
  return absl::StrFormat(
      R"({"write":%s,"read":%s,"tcp_connect_failures":%u,"rpc_requests_received":%u})",
      FormatOpMetricsJson(t.write), FormatOpMetricsJson(t.read),
      t.tcp_connect_failures, t.rpc_requests_received);
}

}  // namespace

std::string FormatNodeMetricsJson(const NodeMetricsSnapshot& s) {
  return absl::StrFormat(
      R"({"num_instances":%d,"xfer_size_bytes":%u,"cpu":%s,"transport":%s})",
      s.num_instances, s.xfer_size_bytes, FormatCpuStatsJson(s.cpu),
      FormatTransportMetricsJson(s.transport));
}

absl::StatusOr<std::vector<PeerTarget>> ParseNodeReadyLine(
    std::string_view line) {
  std::string_view trimmed = absl::StripAsciiWhitespace(line);
  if (!absl::StartsWith(trimmed, kNodeReadyPrefix)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Missing ", kNodeReadyPrefix, " prefix in: ", line));
  }

  const auto tg_start = trimmed.find("\"targets\":[");
  if (tg_start == std::string_view::npos) {
    return absl::InvalidArgumentError(
        "Missing \"targets\" array in ready line");
  }
  std::string_view tg_part = trimmed.substr(tg_start + 11);
  const auto tg_end = tg_part.find(']');
  if (tg_end == std::string_view::npos) {
    return absl::InvalidArgumentError(
        "Unterminated \"targets\" array in ready line");
  }
  tg_part = tg_part.substr(0, tg_end);

  std::vector<std::string_view> entries;
  for (std::string_view tok : absl::StrSplit(tg_part, ',', absl::SkipEmpty())) {
    tok = absl::StripAsciiWhitespace(tok);
    if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
      tok = tok.substr(1, tok.size() - 2);
    }
    if (!tok.empty()) {
      entries.push_back(tok);
    }
  }
  if (entries.empty()) {
    return absl::InvalidArgumentError("Empty \"targets\" array in ready line");
  }
  return ParseTargets(absl::StrJoin(entries, ","));
}

void ControlChannel::EmitNodeReady(absl::Span<const PeerTarget> targets) {
  std::string joined =
      absl::StrJoin(targets, ",", [](std::string* out, const PeerTarget& t) {
        absl::StrAppendFormat(out, "\"%s@%u\"", t.endpoint, t.raddr);
      });
  EmitLine(kNodeReadyPrefix, absl::StrFormat(R"({"targets":[%s]})", joined));
}

bool ControlChannel::WaitForCommand(std::string_view expected_cmd) {
  std::string line;
  while (std::getline(in_, line)) {
    std::string_view trimmed = absl::StripAsciiWhitespace(line);
    if (trimmed == expected_cmd) {
      return true;
    }
  }
  return false;
}

void ControlChannel::EmitMetricsSample(const NodeMetricsSnapshot& snapshot) {
  EmitLine(kMetricsSamplePrefix, FormatNodeMetricsJson(snapshot));
}

void ControlChannel::EmitWorkloadDone(const NodeMetricsSnapshot& snapshot) {
  EmitLine(kWorkloadDonePrefix, FormatNodeMetricsJson(snapshot));
}

void ControlChannel::EmitNodeDone(const NodeMetricsSnapshot& snapshot) {
  EmitLine(kNodeDonePrefix, FormatNodeMetricsJson(snapshot));
}

void ControlChannel::EmitLine(std::string_view prefix,
                              std::string_view payload) {
  absl::MutexLock lock(out_mu_);
  out_ << prefix << payload << "\n";
  out_.flush();
}

}  // namespace peregrine::cluster
