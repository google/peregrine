#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPE_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPE_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>

namespace peregrine::internal {

// This value class defines channel type.
// It is thread-safe since it is immutable.
class ChannelType {
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
  // A flag enum that specifies channel properties.
  enum Property : uint8_t {
    kFake = 1 << 3,      // 0 for real, 8 for fake
    kSide = 1 << 2,      // 0 for one-sided, 4 for two-sided
    kBoundary = 1 << 1,  // 0 for stream, 2 for message
    kLoss = 1 << 0,      // 0 for lossless, 1 for lossy
  };
  using T = std::underlying_type<Property>::type;
  constexpr T ToInt(Property p) const { return static_cast<T>(p); }

 private:
  const uint8_t t_;
};

// See the Property enum above for the bit flags.
inline constexpr ChannelType ChannelType::kTCP(0 | 4 | 0 | 0);
inline constexpr ChannelType ChannelType::kUDP(0 | 4 | 2 | 1);
inline constexpr ChannelType ChannelType::kRDMA(0 | 0 | 2 | 0);
inline constexpr ChannelType ChannelType::kMemMsg(8 | 4 | 2 | 1);
inline constexpr ChannelType ChannelType::kMemStream(8 | 4 | 0 | 0);

inline std::ostream& operator<<(std::ostream& os, ChannelType t) {
  return os << t.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TYPE_H_
