#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>

namespace peregrine::internal {

// This value class defines channel type.
// It is thread-safe since it is immutable.
class ChannelType {
 private:
  // A flag enum that specifies channel properties.
  enum Property : uint8_t {
    kLoss = 1 << 0,      // 0 for lossless, 1 for lossy
    kBoundary = 1 << 1,  // 0 for stream, 1 for message
    kSide = 1 << 2,      // 0 for one-sided, 1 for two-sided
    kFake = 1 << 3,      // 0 for real, 1 for fake
  };
  using T = std::underlying_type<Property>::type;
  constexpr T ToInt(Property p) const { return static_cast<T>(p); }

 public:
  static const ChannelType kTCP;
  static const ChannelType kUDP;
  static const ChannelType kRDMA;
  static const ChannelType kMemMsg;
  static const ChannelType kMemStream;

  // Returns true iff the channel is lossless (e.g., TCP/RDMA).
  constexpr bool IsLosslessChannel() const {
    return (t_ & ToInt(Property::kLoss)) == 0;
  }

  // Returns true iff the channel is lossy (e.g., UDP).
  constexpr bool IsLossyChannel() const { return !IsLosslessChannel(); }

  // Returns true iff the channel is stream (e.g., TCP).
  constexpr bool IsStreamChannel() const {
    return (t_ & ToInt(Property::kBoundary)) == 0;
  }

  // Returns true iff the channel is message (e.g., UDP/RDMA).
  constexpr bool IsMessageChannel() const { return !IsStreamChannel(); }

  // Returns true iff the channel is one-sided (e.g., RDMA).
  constexpr bool IsOneSidedChannel() const {
    return (t_ & ToInt(Property::kSide)) == 0;
  }

  // Returns true iff the channel is two-sided (e.g., TCP/UDP).
  constexpr bool IsTwoSidedChannel() const { return !IsOneSidedChannel(); }

  // Returns true iff the channel is real (e.g., TCP/UDP/RDMA).
  constexpr bool IsRealChannel() const {
    return (t_ & ToInt(Property::kFake)) == 0;
  }

  // Returns true iff the channel is fake (e.g., MemMsg/MemStream for testing).
  constexpr bool IsFakeChannel() const { return !IsRealChannel(); }

  // Equality operator.
  friend constexpr bool operator==(ChannelType a, ChannelType b) = default;

  // Returns a string representation for the channel type.
  std::string ToString() const;

 private:
  // Constructor.
  constexpr explicit ChannelType(uint8_t type) : t_(type) {}

 private:
  const uint8_t t_;
};

inline std::ostream& operator<<(std::ostream& os, ChannelType t) {
  return os << t.ToString();
}

inline constexpr ChannelType ChannelType::kTCP(
    /*kLoss*/ 0 | /*kBoundary*/ 0 | /*kSide*/ 4 | /*kFake*/ 0);
inline constexpr ChannelType ChannelType::kUDP(
    /*kLoss*/ 1 | /*kBoundary*/ 2 | /*kSide*/ 4 | /*kFake*/ 0);
inline constexpr ChannelType ChannelType::kRDMA(
    /*kLoss*/ 0 | /*kBoundary*/ 2 | /*kSide*/ 0 | /*kFake*/ 0);
inline constexpr ChannelType ChannelType::kMemMsg(
    /*kLoss*/ 1 | /*kBoundary*/ 2 | /*kSide*/ 4 | /*kFake*/ 8);
inline constexpr ChannelType ChannelType::kMemStream(
    /*kLoss*/ 0 | /*kBoundary*/ 0 | /*kSide*/ 4 | /*kFake*/ 8);

static_assert(ChannelType::kTCP.IsLosslessChannel());
static_assert(ChannelType::kTCP.IsStreamChannel());
static_assert(ChannelType::kTCP.IsTwoSidedChannel());
static_assert(ChannelType::kTCP.IsRealChannel());

static_assert(ChannelType::kUDP.IsLossyChannel());
static_assert(ChannelType::kUDP.IsMessageChannel());
static_assert(ChannelType::kUDP.IsTwoSidedChannel());
static_assert(ChannelType::kUDP.IsRealChannel());

static_assert(ChannelType::kRDMA.IsLosslessChannel());
static_assert(ChannelType::kRDMA.IsMessageChannel());
static_assert(ChannelType::kRDMA.IsOneSidedChannel());
static_assert(ChannelType::kRDMA.IsRealChannel());

static_assert(ChannelType::kMemMsg.IsLossyChannel());
static_assert(ChannelType::kMemMsg.IsMessageChannel());
static_assert(ChannelType::kMemMsg.IsTwoSidedChannel());
static_assert(ChannelType::kMemMsg.IsFakeChannel());

static_assert(ChannelType::kMemStream.IsLosslessChannel());
static_assert(ChannelType::kMemStream.IsStreamChannel());
static_assert(ChannelType::kMemStream.IsTwoSidedChannel());
static_assert(ChannelType::kMemStream.IsFakeChannel());

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPES_H_
