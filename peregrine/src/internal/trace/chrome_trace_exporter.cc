#include "peregrine/src/internal/trace/chrome_trace_exporter.h"

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

std::string ChromeTraceExporter::Export(absl::Span<const TraceEvent> /*events*/,
                                        std::string_view /*self_endpoint*/) {
  // TODO(yyd): Serialize assembled Post() timelines into Chrome Trace JSON.
  return "";
}

}  // namespace peregrine::internal
