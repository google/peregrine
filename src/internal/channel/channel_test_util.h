#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_

#include <memory>

#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_msg.h"
#include "src/internal/channel/channel_stream.h"

namespace peregrine::internal::testing {

// Creates a memory stream channel.
inline std::unique_ptr<Channel> TestOnly_CreateMemStreamChannel() {
  return std::make_unique<MemStreamChannel>();
}

// Creates a memory message channel.
inline std::unique_ptr<Channel> TestOnly_CreateMemMsgChannel(int error_rate) {
  return std::make_unique<MemMsgChannel>(error_rate);
}

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
