#ifndef PEREGRINE_TEST_BENCHMARK_FLAGS_H_
#define PEREGRINE_TEST_BENCHMARK_FLAGS_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/flags/declare.h"
#include "src/api/transport_types.h"
#include "test/benchmark/types.h"

ABSL_DECLARE_FLAG(std::string, role);
ABSL_DECLARE_FLAG(std::string, ip);
ABSL_DECLARE_FLAG(std::string, transport);
ABSL_DECLARE_FLAG(uint16_t, app_control_port);
ABSL_DECLARE_FLAG(uint16_t, peregrine_control_port);
ABSL_DECLARE_FLAG(int, conn);
ABSL_DECLARE_FLAG(std::string, peer);
ABSL_DECLARE_FLAG(uint64_t, xfer_size);
ABSL_DECLARE_FLAG(uint32_t, num_xfers);
ABSL_DECLARE_FLAG(peregrine::benchmark::WorkloadType, workload);

namespace peregrine::benchmark {

// Parses the transport type ('tcp' or 'rdma').
peregrine::TransportType ParseTransportType();

// Parses the client/server role from a string.
// Accepts 'client' or 'server'.
Role ParseRole();

// Parses and validates the local network interface IP address from --ip.
std::string ParseIp();

// Validates the provided IP address against local machine NICs.
// Enforces that a non-zero, non-loopback IP is provided and matches an
// interface on the machine.
// TODO: Enforce that the provided IP is associated with an active physical
// interface so that incoming and outgoing traffic is routable to it.
std::string ParseIp(std::string_view ip);

// Parses the benchmark application rendezvous control port (listen port for
// server; target destination port for client).
uint16_t ParseAppControlPort();

// Parses the Peregrine gRPC control plane port (listen port for server;
// target destination port for client; client's local listener is ephemeral).
uint16_t ParsePeregrineControlPort();

// Parses the #connections per peer.
int ParseNumConns();

// Parses the peer address.
std::string ParsePeer();

// Parses the transfer size.
uint64_t ParseXferSize();

// Parses the number of transfers.
uint32_t ParseNumXfers();

// Parses the workload type.
WorkloadType ParseWorkloadType();

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_FLAGS_H_
