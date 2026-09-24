#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_

#include <cstdint>

namespace peregrine::internal {

// An enum that specifies the type of a channel.
enum class ChannelType : uint8_t {
  kReliableStream,     // e.g., tcp
  kReliableMessage,    // e.g., rdma
  kUnreliableMessage,  // e.g., udp
};

// Returns true iff the channel type is reliable stream.
constexpr bool IsReliableStream(ChannelType t) {
  return t == ChannelType::kReliableStream;
}

// Returns true iff the channel type is reliable message.
constexpr bool IsReliableMessage(ChannelType t) {
  return t == ChannelType::kReliableMessage;
}

// Returns true iff the channel type is unreliable message.
constexpr bool IsUnreliableMessage(ChannelType t) {
  return t == ChannelType::kUnreliableMessage;
}

// Returns true iff the channel type is reliable or unreliable message.
constexpr bool IsMessageChannel(ChannelType t) {
  return IsReliableMessage(t) || IsUnreliableMessage(t);
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
