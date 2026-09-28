#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_

#include <cstdint>
#include <type_traits>

namespace peregrine::internal {

// An enum that specifies the property of a channel.
// Do not use outside of the channel/ folder.
enum __ChannelProperty : uint8_t {
  kLoss = 1 << 0,      // 0 for lossless, 1 for lossy
  kBoundary = 1 << 1,  // 0 for stream, 1 for message
  kSide = 1 << 2,      // 0 for one-sided, 1 for two-sided
};

// An enum that specifies the concreate channel type.
enum class ChannelType : uint8_t {
  kTCP = /*kLoss*/ 0 | /*kBoundary*/ 0 | /*kSide*/ 4,
  kUDP = /*kLoss*/ 1 | /*kBoundary*/ 2 | /*kSide*/ 4,
  kRDMA = /*kLoss*/ 0 | /*kBoundary*/ 2 | /*kSide*/ 0,
  kMemMsg = kUDP,
  kMemStream = kTCP,
};

using ChP = std::underlying_type<__ChannelProperty>::type;
using ChT = std::underlying_type<ChannelType>::type;
constexpr ChP ToInt(__ChannelProperty p) { return static_cast<ChP>(p); }
constexpr ChT ToInt(ChannelType t) { return static_cast<ChT>(t); }

// Returns true iff the channel is lossless.
constexpr bool IsLosslessChannel(ChannelType t) {
  return (ToInt(t) & ToInt(__ChannelProperty::kLoss)) == 0;
}

// Returns true iff the channel is lossy.
constexpr bool IsLossyChannel(ChannelType t) { return !IsLosslessChannel(t); }

// Returns true iff the channel is stream.
constexpr bool IsStreamChannel(ChannelType t) {
  return (ToInt(t) & ToInt(__ChannelProperty::kBoundary)) == 0;
}

// Returns true iff the channel is message.
constexpr bool IsMessageChannel(ChannelType t) { return !IsStreamChannel(t); }

// Returns true iff the channel is one-sided (sender code only, like RDMA).
constexpr bool IsOneSidedChannel(ChannelType t) {
  return (ToInt(t) & ToInt(__ChannelProperty::kSide)) == 0;
}

// Returns true iff the channel is two-sided (sender/receiver, like TCP/UDP).
constexpr bool IsTwoSidedChannel(ChannelType t) {
  return !IsOneSidedChannel(t);
}

static_assert(IsLosslessChannel(ChannelType::kTCP));
static_assert(IsStreamChannel(ChannelType::kTCP));
static_assert(IsTwoSidedChannel(ChannelType::kTCP));

static_assert(IsLossyChannel(ChannelType::kUDP));
static_assert(IsMessageChannel(ChannelType::kUDP));
static_assert(IsTwoSidedChannel(ChannelType::kUDP));

static_assert(IsLosslessChannel(ChannelType::kRDMA));
static_assert(IsMessageChannel(ChannelType::kRDMA));
static_assert(IsOneSidedChannel(ChannelType::kRDMA));

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
