#ifndef PEREGRINE_TEST_CLUSTER_NODE_CONTROL_CHANNEL_H_
#define PEREGRINE_TEST_CLUSTER_NODE_CONTROL_CHANNEL_H_

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/metrics.h"

namespace peregrine::cluster {

inline constexpr std::string_view kNodeReadyPrefix = "PEREGRINE_NODE_READY ";
inline constexpr std::string_view kMetricsSamplePrefix =
    "PEREGRINE_METRICS_SAMPLE ";
inline constexpr std::string_view kWorkloadDonePrefix =
    "PEREGRINE_WORKLOAD_DONE ";
inline constexpr std::string_view kNodeDonePrefix = "PEREGRINE_NODE_DONE ";

inline constexpr std::string_view kStartCommand = "START";
inline constexpr std::string_view kStopCommand = "STOP";

// Serializes a `NodeMetricsSnapshot` into a single-line JSON string.
std::string FormatNodeMetricsJson(const NodeMetricsSnapshot& snapshot);

// Parses a `PEREGRINE_NODE_READY {"targets":["<ep>@<raddr>",...]}` line into a
// list of `PeerTarget` structs.
absl::StatusOr<std::vector<PeerTarget>> ParseNodeReadyLine(
    std::string_view line);

// Thread-safe stdio control channel used by `ClusterNode` to coordinate the
// 4-phase barrier lifecycle (`NODE_READY` -> `START` -> `WORKLOAD_DONE` ->
// `STOP` -> `NODE_DONE`) with the Python orchestrator.
class ControlChannel {
 public:
  explicit ControlChannel(std::istream& in = std::cin,
                          std::ostream& out = std::cout)
      : in_(in), out_(out) {}

  // Phase 1: Emits `PEREGRINE_NODE_READY {"targets":["<ep>@<raddr>",...]}\n`.
  void EmitNodeReady(absl::Span<const PeerTarget> targets);

  // Phase 2 & 4: Blocks reading lines from `in_` until `expected_cmd` (e.g.
  // "START" or "STOP") is received, or returns false on EOF.
  bool WaitForCommand(std::string_view expected_cmd);

  // Phase 3 (periodic): Emits `PEREGRINE_METRICS_SAMPLE <json>\n`.
  void EmitMetricsSample(const NodeMetricsSnapshot& snapshot);

  // Phase 3 (completion): Emits `PEREGRINE_WORKLOAD_DONE <json>\n`.
  void EmitWorkloadDone(const NodeMetricsSnapshot& snapshot);

  // Phase 4 (teardown): Emits `PEREGRINE_NODE_DONE <json>\n`.
  void EmitNodeDone(const NodeMetricsSnapshot& snapshot);

 private:
  void EmitLine(std::string_view prefix, std::string_view payload);

  std::istream& in_;
  absl::Mutex out_mu_;
  std::ostream& out_ ABSL_GUARDED_BY(out_mu_);
};

}  // namespace peregrine::cluster

#endif  // PEREGRINE_TEST_CLUSTER_NODE_CONTROL_CHANNEL_H_
