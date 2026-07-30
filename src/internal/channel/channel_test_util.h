#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_

#include <sys/socket.h>

#include <memory>
#include <string>

#include "src/internal/channel/channel.h"

namespace peregrine::internal::testing {

enum class TestChannelType {
  kTcp,
  kUdp,
  kMemStream,
  kMemMsg,
};

// Returns a string representation for the test channel type.
std::string ToString(TestChannelType t);

// A pair of connected channels.
struct ConnectedChannelPair final {
  std::unique_ptr<Channel> sndr;
  std::unique_ptr<Channel> rcvr;

  // Creates a tcp channel pair in the given address family.
  static ConnectedChannelPair CreateTcp(int family);

  // Creates a udp channel pair in the given address family.
  static ConnectedChannelPair CreateUdp(int family);

  // Creates a memory stream channel pair.
  static ConnectedChannelPair CreateMemStream(int error_rate);

  // Creates a memory message channel pair.
  static ConnectedChannelPair CreateMemMsg(int error_rate);
};

// Creates a test channel pair with the given type and error rate.
ConnectedChannelPair CreateTestChannelPair(TestChannelType type,
                                           int family = AF_INET,
                                           int error_rate = 0);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TEST_UTIL_H_
