#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_PARSER_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_PARSER_H_

#include <string>
#include <string_view>

#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

// This utility class implements (de)serialization for control messages.
// It is thread-safe since it has no data members.
class ControlMsg final {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessage);

 public:
  // Serializes the control message to a string.
  static std::string Serialize(const proto::Control& c) {
    return c.SerializeAsString();
  }

  // Parses the control message from a string.
  // Returns true iff parsing is successful.
  static bool Deserialize(std::string_view s, proto::Control& c) {
    return c.ParseFromString(s);
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
