#include <sys/socket.h>

#include <cstdint>
#include <string>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "src/api/transport_types.h"
#include "test/benchmark/flags.h"
#include "test/benchmark/peregrine_util.h"
#include "test/benchmark/types.h"

namespace {
using ::peregrine::benchmark::ParseAppControlPort;
using ::peregrine::benchmark::ParseIp;
using ::peregrine::benchmark::ParseNumConns;
using ::peregrine::benchmark::ParseNumXfers;
using ::peregrine::benchmark::ParsePeer;
using ::peregrine::benchmark::ParsePeregrineControlPort;
using ::peregrine::benchmark::ParseRole;
using ::peregrine::benchmark::ParseXferSize;
using ::peregrine::benchmark::Role;
using ::peregrine::benchmark::RunClient;
using ::peregrine::benchmark::RunServer;
}  // namespace

int main(int argc, char* argv[]) {
  // Initialize logging and flags.
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Parse cmd line flags.
  const std::string ip = ParseIp();
  const Role role = ParseRole();
  const peregrine::TransportType transport_type =
      ::peregrine::benchmark::ParseTransportType();
  const uint16_t app_control_port = ParseAppControlPort();
  const uint16_t peregrine_control_port = ParsePeregrineControlPort();
  const int nconns = ParseNumConns();
  const std::string peer = (role == Role::kClient) ? ParsePeer() : "";
  const uint32_t num_xfers = ParseNumXfers();
  const uint64_t xfer_size = ParseXferSize();

  // Run server or client.
  // NOTE: The benchmark runs in a strictly sequential (serial) lock-step mode:
  // 1. For each iteration, the client requests a remote memory address over the
  //    TCP control channel.
  // 2. The server allocates/returns the destination memory pointer.
  // 3. The client posts a single write and blocks (polls) until it completes
  //    successfully before starting the next transfer iteration.
  // Parallel streams/concurrent writes are currently not supported.
  if (role == Role::kServer) {
    RunServer(ip, peregrine_control_port, app_control_port, nconns, xfer_size,
              transport_type);
  } else {
    RunClient(ip, peregrine_control_port, app_control_port, nconns, xfer_size,
              peer, num_xfers, transport_type);
  }
  return 0;
}
