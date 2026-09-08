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
using ::peregrine::benchmark::ParsePeer;
using ::peregrine::benchmark::ParsePeregrineControlPort;
using ::peregrine::benchmark::ParseRole;
using ::peregrine::benchmark::ParseWorkloadType;
using ::peregrine::benchmark::ParseXferSize;
using ::peregrine::benchmark::Role;
using ::peregrine::benchmark::RunClient;
using ::peregrine::benchmark::RunServer;
using ::peregrine::benchmark::WorkloadType;
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
  const WorkloadType workload = ParseWorkloadType();

  // Run server or client.
  if (role == Role::kServer) {
    const uint64_t xfer_size = ParseXferSize();
    RunServer(ip, peregrine_control_port, app_control_port, nconns, xfer_size,
              transport_type);
  } else {
    RunClient(ip, peregrine_control_port, app_control_port, nconns, peer,
              transport_type, workload);
  }
  return 0;
}
