#include <sys/socket.h>

#include <cstdint>
#include <string>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "test/benchmark/flags.h"
#include "test/benchmark/peregrine_util.h"
#include "test/benchmark/types.h"

namespace {
using ::peregrine::benchmark::ParseIp;
using ::peregrine::benchmark::ParseIPver;
using ::peregrine::benchmark::ParseNumConns;
using ::peregrine::benchmark::ParseNumXfers;
using ::peregrine::benchmark::ParsePeer;
using ::peregrine::benchmark::ParsePort;
using ::peregrine::benchmark::ParseRole;
using ::peregrine::benchmark::ParseXferSize;
using ::peregrine::benchmark::Role;
using ::peregrine::benchmark::RunRcvr;
using ::peregrine::benchmark::RunSndr;
}  // namespace

int main(int argc, char* argv[]) {
  // Initialize logging and flags.
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Parse cmd line flags.
  const bool ipv4 = ParseIPver();
  const std::string ip = ParseIp();
  const Role role = ParseRole();
  const uint16_t port = ParsePort();
  const int nconns = ParseNumConns();
  const std::string peer = (role == Role::kSndr) ? ParsePeer() : "";
  const uint32_t num_xfers = ParseNumXfers();
  const uint64_t xfer_size = ParseXferSize();

  // Run receiver or sender.
  // NOTE: The benchmark runs in a strictly sequential (serial) lock-step mode:
  // 1. For each iteration, the sender requests a remote memory address over the
  //    TCP control channel.
  // 2. The receiver allocates/returns the destination memory pointer.
  // 3. The sender posts a single write and blocks (polls) until it completes
  //    successfully before starting the next transfer iteration.
  // Parallel streams/concurrent writes are currently not supported.
  if (role == Role::kRcvr) {
    RunRcvr(ipv4, ip, port, nconns, xfer_size);
  } else {
    RunSndr(ipv4, ip, port, nconns, xfer_size, peer, num_xfers);
  }
  return 0;
}
