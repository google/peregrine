#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_

#include <memory>

#include "src/internal/channel/channel.h"

namespace peregrine::internal::testing {

// A pair of connected channels.
struct ConnectedChannelPair final {
  std::unique_ptr<Channel> sndr;
  std::unique_ptr<Channel> rcvr;

  // Creates a tcp channel pair in the given address family.
  static ConnectedChannelPair CreateTcp(int family);

  // Creates a udp channel pair in the given address family.
  static ConnectedChannelPair CreateUdp(int family);

  // Creates a memory stream channel pair.
  static ConnectedChannelPair CreateMemStream();

  // Creates a memory message channel pair.
  static ConnectedChannelPair CreateMemMsg(int error_rate);
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
