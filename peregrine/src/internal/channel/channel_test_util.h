#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_

#include <sys/socket.h>

#include <memory>

#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/util/test_param.h"

namespace peregrine::internal::testing {

// A pair of connected channels.
struct ConnectedChannelPair final {
  std::unique_ptr<Channel> sndr;
  std::unique_ptr<Channel> rcvr;
};

// Creates a tcp channel pair.
ConnectedChannelPair CreateTcpChannelPair(int family, bool blocking);

// Creates a udp channel pair.
ConnectedChannelPair CreateUdpChannelPair(int family, bool blocking);

// Creates a memory stream channel pair.
ConnectedChannelPair CreateMemStreamChannelPair(int error_rate);

// Creates a memory message channel pair.
ConnectedChannelPair CreateMemMsgChannelPair(int error_rate);

// Creates a test channel pair.
ConnectedChannelPair CreateTestChannelPair(TestChannelType type, int family,
                                           bool blocking, int error_rate);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
