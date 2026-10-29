#ifndef PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

// Cold-path exporter that translates TraceEvent snapshots into Chrome Trace
// Event / Perfetto JSON format (https://ui.perfetto.dev / chrome://tracing).
class ChromeTraceExporter final {
 public:
  ChromeTraceExporter() = delete;

  // Exports buffered TraceEvent records in Chrome Trace Event JSON format.
  static std::string Export(absl::Span<const TraceEvent> events,
                            std::string_view self_endpoint = "");
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_CHROME_TRACE_EXPORTER_H_
