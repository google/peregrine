#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

// This utility class provides conversion between control messages and
// their proto representations.
class Message final {
  static_assert(assumptions::kTcpListenersOfControlAndDataPlanesAreSeparate);
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessageAtMost1KiB);

 public:
  // The maximum length of a serialized control message.
  static constexpr size_t kMaxLen = 1024;

  // Serializes the control message to a string.
  static std::string Serialize(const proto::ReqMsg& msg) {
    return msg.SerializeAsString();
  }

  // Parses the control message from a string.
  // Returns true iff parsing is successful.
  static bool Deserialize(std::string_view s, proto::ReqMsg& msg) {
    return msg.ParseFromString(s);
  }

  // Converts peer requests to its proto.
  static bool Convert(const HostInfo& host, absl::Span<const Request> requests,
                      proto::ReqMsg& msg);

  // Converts a proto to its peer requests.
  static std::pair<HostInfo, std::vector<Request>> Convert(
      const proto::ReqMsg& msg);

  // Returns true iff the proto requests are equal.
  static bool AreEqual(const proto::Request& a, const proto::Request& b) {
    return a.op() == b.op() && a.laddr() == b.laddr() &&
           a.raddr() == b.raddr() && a.len() == b.len();
  }

 private:
  // Converts a request to its proto.
  static bool convert(const HostInfo& host, proto::HostInfo& proto);

  // Converts a proto to its request.
  static bool convert(const proto::HostInfo& proto, HostInfo& host);

  // Converts a request to its proto.
  static bool convert(const Request& r, proto::Request& proto);

  // Converts a proto to its request.
  static bool convert(const proto::Request& proto, Request& r);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_
