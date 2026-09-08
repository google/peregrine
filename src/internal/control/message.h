#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/socket/psp/psp.h"

namespace peregrine::internal {

// This utility class provides conversion between control messages and
// their proto representations.
class Message final {
  static_assert(assumptions::kTcpListenersOfControlAndDataPlanesAreSeparate);
  static_assert(assumptions::kThereIsOnlyOnePairOfWrapperControlMessages);

 public:
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
  static bool Serialize(const HostInfo& host, absl::Span<const Request> reqs,
                        proto::ReqMsg& msg);

  // Converts a proto to its peer requests.
  static std::pair<HostInfo, std::vector<Request>> Deserialize(
      const proto::ReqMsg& msg);

  // Returns true iff the proto requests are equal.
  static bool AreEqual(const proto::Request& a, const proto::Request& b) {
    return a.op() == b.op() && a.laddr() == b.laddr() &&
           a.raddr() == b.raddr() && a.len() == b.len() && a.rkey() == b.rkey();
  }

  // Serializes the host info to its proto.
  // Returns true iff serialization is successful.
  static bool Serialize(const HostInfo& host, proto::HostInfo& proto);

  // Deserializes the host info from its proto.
  // Returns true iff deserialization is successful.
  static bool Deserialize(const proto::HostInfo& proto, HostInfo& host);

  // Serializes the PSP token and peer target to its proto.
  // Returns true iff serialization is successful.
  static bool Serialize(const PspToken& token, const Endpoint& peer_target,
                        proto::PspTcpReq& proto);

  // Deserializes the PSP token and peer target from its proto.
  // Returns true iff deserialization is successful.
  static bool Deserialize(const proto::PspTcpReq& proto, PspToken& token,
                          Endpoint& peer_target);

  // Serializes the PSP token to its proto.
  // Returns true iff serialization is successful.
  static bool Serialize(const PspToken& token, proto::PspTcpResp& proto);

  // Deserializes the PSP token from its proto.
  // Returns true iff deserialization is successful.
  static bool Deserialize(const proto::PspTcpResp& proto, PspToken& token);

 private:  // common message components
  static bool serialize(const Request& r, proto::Request& proto);
  static bool deserialize(const proto::Request& proto, Request& r);

  static bool serialize(const Endpoint& endpoint, proto::Endpoint& proto);
  static bool deserialize(const proto::Endpoint& proto, Endpoint& endpoint);

  static bool serialize(const PspToken& token, proto::PspToken& proto);
  static bool deserialize(const proto::PspToken& proto, PspToken& token);

  static bool serialize(const RdmaNic& rdma, proto::RdmaNic& proto);
  static bool deserialize(const proto::RdmaNic& proto, RdmaNic& rdma);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_MESSAGE_H_
