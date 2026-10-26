#ifndef PEREGRINE_SRC_API_TRANSPORT_TRACE_H_
#define PEREGRINE_SRC_API_TRANSPORT_TRACE_H_

#include <cstdint>

namespace peregrine {

// Configuration for transport request tracing.
struct TraceConfig final {
  // Sampling interval for Transport::Post() calls:
  //   0 = tracing disabled (default)
  //   1 = always-on (trace every Post() call)
  //   N > 1 = trace 1 out of every N Post() calls
  uint32_t sample_every_n = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_TRACE_H_
