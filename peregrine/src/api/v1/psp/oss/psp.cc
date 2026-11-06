#include "peregrine/src/api/v1/psp/psp.h"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/channel.h"

ABSL_FLAG(bool, enable_psp_tcp, false,
          "Whether to enable PSP encryption for TCP connections. It is not "
          "needed in Google Cloud since the traffic is already encrypted. "
          "See https://cloud.google.com/docs/security/encryption-in-transit).");

namespace peregrine::v1 {

bool IsPspSupported() { return false; }

bool IsPspEnabled(int client_fd) { return false; }

absl::StatusOr<PspToken> PspRegisterPeer(int listener_fd, uint32_t client_spi,
                                         std::string_view client_key,
                                         std::string_view peer_target) {
  return absl::UnimplementedError("psp not supported.");
}

absl::Status PspTcpConnect(int fd, const struct sockaddr* addr,
                           socklen_t addrlen,
                           std::shared_ptr<grpc::Channel> channel,
                           int64_t* psp_key_xchg_us, int64_t* tcp_connect_us) {
  return absl::UnimplementedError("psp not supported.");
}

}  // namespace peregrine::v1
