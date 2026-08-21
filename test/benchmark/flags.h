#ifndef PEREGRINE_TEST_BENCHMARK_FLAGS_H_
#define PEREGRINE_TEST_BENCHMARK_FLAGS_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/flags/declare.h"
#include "test/benchmark/types.h"

ABSL_DECLARE_FLAG(std::string, role);
ABSL_DECLARE_FLAG(std::string, ip);
ABSL_DECLARE_FLAG(bool, ipv4);
ABSL_DECLARE_FLAG(uint16_t, port);
ABSL_DECLARE_FLAG(uint16_t, control_port);
ABSL_DECLARE_FLAG(int, conn);
ABSL_DECLARE_FLAG(std::string, peer);
ABSL_DECLARE_FLAG(uint64_t, xfer_size);
ABSL_DECLARE_FLAG(uint32_t, num_xfers);

namespace peregrine::benchmark {

// Parses the sender/receiver role from a string.
// Fails if the role is not 'sender|send|s' or 'receiver|recv|r'.
Role ParseRole();

// Parses and validates the local network interface IP address from --ip.
std::string ParseIp();

// Validates the provided IP address against local machine NICs.
// Enforces that a non-zero, non-loopback IP is provided and matches an
// interface on the machine.
// TODO: Enforce that the provided IP is associated with an active physical
// interface so that incoming and outgoing traffic is routable to it.
std::string ParseIp(std::string_view ip);

// Parses the IPv4/IPv6 mode.
bool ParseIPver();

// Parses the local port.
uint16_t ParsePort();

// Parses the control port.
uint16_t ParseControlPort();

// Parses the #connections per peer.
int ParseNumConns();

// Parses the peer address.
std::string ParsePeer();

// Parses the transfer size.
uint64_t ParseXferSize();

// Parses the number of transfers.
uint32_t ParseNumXfers();

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_FLAGS_H_
