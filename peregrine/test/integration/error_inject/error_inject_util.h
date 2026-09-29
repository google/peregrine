#ifndef PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECT_UTIL_H_
#define PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECT_UTIL_H_

#include <memory>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "peregrine/test/integration/error_inject/error_injector.h"
#include "peregrine/test/integration/error_inject/mid_chunk_disconnect.h"

namespace peregrine::integration {

// Parses the error injector type from its string representation.
inline ErrorInjectorType ParseErrorInjectorType(std::string_view s) {
  if (absl::EqualsIgnoreCase(s, "none")) {
    return ErrorInjectorType::kNone;
  } else if (absl::EqualsIgnoreCase(s, "mid_chunk_disconnect")) {
    return ErrorInjectorType::kMidChunkDisconnect;
  } else {
    LOG(FATAL) << "invalid error_inject: " << s
               << ". Expected 'none' or 'mid_chunk_disconnect'.";
  }
}

// Factory function to instantiate the requested error injector.
// Returns nullptr when `type` is ErrorInjectorType::kNone.
inline std::unique_ptr<ErrorInjector> CreateErrorInjector(
    ErrorInjectorType type) {
  switch (type) {
    case ErrorInjectorType::kNone:
      return nullptr;
    case ErrorInjectorType::kMidChunkDisconnect:
      return MidChunkDisconnect::Create();
  }
  LOG(FATAL) << "Unknown ErrorInjectorType: " << static_cast<int>(type);
}

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_ERROR_INJECT_ERROR_INJECT_UTIL_H_
