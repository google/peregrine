#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_PARSER_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_PARSER_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

// This utility class implements (de)serialization for control messages.
// It is thread-safe since it has no data members.
class ControlMsg final {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessage);

 public:
  // The maximum length of a serialized control message.
  static constexpr size_t kMaxLen = 512;

  // Serializes the control message to a string.
  static std::string Serialize(const proto::ControlReq& msg) {
    return msg.SerializeAsString();
  }

  // Parses the control message from a string.
  // Returns true iff parsing is successful.
  static bool Deserialize(std::string_view s, proto::ControlReq& msg) {
    return msg.ParseFromString(s);
  }

 private:
  // Serializes the request to a string.
  static std::string serialize(const Request& r);

  // Parses the request from a string.
  // Returns true iff parsing is successful.
  static bool deserialize(std::string_view s, Request& r);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_PARSER_H_
