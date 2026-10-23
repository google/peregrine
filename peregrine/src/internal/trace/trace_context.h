#ifndef PEREGRINE_SRC_INTERNAL_TRACE_TRACE_CONTEXT_H_
#define PEREGRINE_SRC_INTERNAL_TRACE_TRACE_CONTEXT_H_

#include <cstdint>

namespace peregrine::internal {

// Lightweight context propagated across threads for a single Post() call.
struct TraceContext final {
  uint8_t is_traced : 1;
  uint8_t unused : 7;

  // Default constructor.
  TraceContext() : is_traced(0), unused(0) {}

  // Constructor.
  explicit TraceContext(bool is_traced) : is_traced(is_traced), unused(0) {}
};
static_assert(sizeof(TraceContext) == 1);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRACE_TRACE_CONTEXT_H_
