#include "peregrine/src/internal/channel/channel_types.h"

#include <string>

namespace peregrine::internal {

std::string ChannelType::ToString() const {
  if (*this == ChannelType::kTCP) return "TcpChannel";
  if (*this == ChannelType::kUDP) return "UdpChannel";
  if (*this == ChannelType::kRDMA) return "RdmaChannel";
  if (*this == ChannelType::kMemMsg) return "MemMessageChannel";
  if (*this == ChannelType::kMemStream) return "MemStreamChannel";
  return "UnknownChannel";
}

}  // namespace peregrine::internal
