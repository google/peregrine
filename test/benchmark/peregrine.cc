#include <sys/socket.h>

#include <cstdint>
#include <string>

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "test/benchmark/flags.h"
#include "test/benchmark/peregrine_util.h"
#include "test/benchmark/types.h"

namespace {
using ::peregrine::benchmark::ParseIPver;
using ::peregrine::benchmark::ParseNumXfers;
using ::peregrine::benchmark::ParsePeer;
using ::peregrine::benchmark::ParsePort;
using ::peregrine::benchmark::ParseRemoteAddress;
using ::peregrine::benchmark::ParseRole;
using ::peregrine::benchmark::ParseXferSize;
using ::peregrine::benchmark::Role;
using ::peregrine::benchmark::RunRcvr;
using ::peregrine::benchmark::RunSndr;
}  // namespace

int main(int argc, char* argv[]) {
  // Initialize logging.
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Parse cmd line flags.
  const bool ipv4 = ParseIPver();
  const Role role = ParseRole();
  const uint16_t port = ParsePort();
  const std::string peer = ParsePeer();
  void* const raddr = ParseRemoteAddress();
  const uint32_t num_xfers = ParseNumXfers();
  const uint64_t xfer_size = ParseXferSize();

  // Run receiver or sender.
  if (role == Role::kRcvr) {
    RunRcvr(ipv4, port, xfer_size);
  } else {
    RunSndr(ipv4, port, xfer_size, peer, raddr, num_xfers);
  }
  return 0;
}
