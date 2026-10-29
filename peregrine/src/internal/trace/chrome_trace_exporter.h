#ifndef PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

// Cold-path exporter that translates TraceEvent snapshots into Chrome Trace
// Event / Perfetto JSON format (https://ui.perfetto.dev / chrome://tracing).
//
// Track (pid / tid) mapping:
//   - pid: 1 (Sender), tid: 1: Caller / Poller Thread
//   - pid: 1 (Sender), tid: 2: Engine::main_thread_
//   - pid: 1 (Sender), tid: 100 + i: Send Worker #i (Worker::send_thread_)
//   - pid: 2 (Receiver), tid: 200 + j: Remote Recv Worker #j
//     (Worker::recv_thread_)
//   - pid: 1 (Sender), tid: 300 + i: Recv ACK Worker #i
//     (Worker::recv_thread_)
class ChromeTraceExporter final {
 public:
  ChromeTraceExporter() = delete;

  // Correlates raw TraceEvent records by (Handle, ReqId, chunk_t) into
  // per-Post() timelines and exports them in Chrome Trace Event JSON format.
  static std::string Export(absl::Span<const TraceEvent> events,
                            std::string_view self_endpoint = "");
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_
