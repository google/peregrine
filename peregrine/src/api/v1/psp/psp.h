#ifndef PEREGRINE_SRC_API_V1_PSP_H_
#define PEREGRINE_SRC_API_V1_PSP_H_

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include "absl/flags/declare.h"
#include "absl/log/log.h"
#include "grpcpp/channel.h"
#include "peregrine/src/internal/socket/psp/psp.h"

ABSL_DECLARE_FLAG(bool, enable_psp_tcp);
ABSL_DECLARE_FLAG(bool, peregrine_enable_psp_tcp);

namespace peregrine::v1 {

using PspToken = internal::PspToken;

// Returns true iff psp tcp is supported.
bool IsPspSupported();

// Returns true iff the socket `fd` has psp encryption enabled.
bool IsPspEnabled(int fd);

// Registers a client's psp token on the socket `listener_fd` and
// returns the allocated listener psp token.
absl::StatusOr<PspToken> PspRegisterPeer(int listener_fd, uint32_t client_spi,
                                         std::string_view client_key,
                                         std::string_view peer_target);

// Performs out-of-band psp key exchange on the socket `fd` using the provided
// gRPC channel and connects to `addr`.
absl::Status PspTcpConnect(int fd, const struct sockaddr* addr,
                           socklen_t addrlen,
                           std::shared_ptr<grpc::Channel> channel,
                           int64_t* psp_key_xchg_us = nullptr,
                           int64_t* tcp_connect_us = nullptr);

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_PSP_H_
