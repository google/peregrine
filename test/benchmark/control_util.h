#ifndef PEREGRINE_TEST_BENCHMARK_CONTROL_UTIL_H_
#define PEREGRINE_TEST_BENCHMARK_CONTROL_UTIL_H_

#include <cstdint>
#include <string_view>

#include "test/benchmark/control.pb.h"

namespace peregrine::benchmark {

// Sends a ControlMessage protobuf over TCP.
bool SendControlMessage(int fd, const proto::ControlMessage& msg);

// Receives and parses a ControlMessage protobuf over TCP.
bool ProcessControlMessage(int fd, proto::ControlMessage* msg);

// Creates a TCP socket listening on the specified port.
int CreateControlListener(bool ipv4, uint16_t port);

// Connects via TCP to a remote control host with retries.
int ConnectControlWithRetry(bool ipv4, std::string_view host, uint16_t port);

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_CONTROL_UTIL_H_
