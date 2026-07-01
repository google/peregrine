#ifndef PEREGRINE_TEST_BENCHMARK_FLAGS_H_
#define PEREGRINE_TEST_BENCHMARK_FLAGS_H_

#include <cstdint>
#include <string>

#include "absl/flags/declare.h"
#include "test/benchmark/types.h"

ABSL_DECLARE_FLAG(std::string, role);
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
