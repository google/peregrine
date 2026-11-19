#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_POST_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_POST_H_

#include <vector>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/trace/trace_context.h"

namespace peregrine::internal::engine2 {

// A batch of requests to one peer, identified by `handle`.
struct Post {
  Handle handle;
  std::vector<Request> requests;
  Metadata metadata = {};
  TraceContext trace_ctx;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_POST_H_
