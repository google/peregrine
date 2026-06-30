#include <algorithm>
#include <cstdint>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "test/benchmark/types.h"

ABSL_FLAG(bool, ipv4, true, "Use IPv4 if true, otherwise IPv6");

ABSL_FLAG(std::string, role, "receiver",
          "Role to run as: 'sender|send|s' or 'receiver|recv|r'");

ABSL_FLAG(uint16_t, port, 0,
          "Local port to listen on (0 let the operating system choose)");

ABSL_FLAG(int, conn, 8,
          "#Connections to make between this process and each peer");

ABSL_FLAG(std::string, peer, "",
          "Receiver ip_address:port (required for sender)");

ABSL_FLAG(std::string, raddr, "0x0",
          "Remote buffer address in hex/decimal (required for sender)");

ABSL_FLAG(uint64_t, xfer_size, 1024 * 1024 * 1024ULL,
          "Buffer transfer size in bytes (default = 1 GiB)");

ABSL_FLAG(uint32_t, num_xfers, 100,
          "Number of transfers to perform (default = 100)");

namespace peregrine::benchmark {

Role ParseRole() {
  const std::string s = absl::GetFlag(FLAGS_role);
  if (absl::EqualsIgnoreCase(s, "sender") ||
      absl::EqualsIgnoreCase(s, "send") || absl::EqualsIgnoreCase(s, "s")) {
    return Role::kSndr;

  } else if (absl::EqualsIgnoreCase(s, "receiver") ||
             absl::EqualsIgnoreCase(s, "recv") ||
             absl::EqualsIgnoreCase(s, "r")) {
    return Role::kRcvr;

  } else {
    LOG(FATAL) << "invalid role: " << s;
  }
}

bool ParseIPver() { return absl::GetFlag(FLAGS_ipv4); }

uint16_t ParsePort() { return absl::GetFlag(FLAGS_port); }

int ParseNumConns() {
  const int v = absl::GetFlag(FLAGS_conn);
  return std::min(std::max(1, v), 100);
}

std::string ParsePeer() { return absl::GetFlag(FLAGS_peer); }

void* ParseRemoteAddress() {
  std::string s = absl::GetFlag(FLAGS_raddr);
  uint64_t v = 0;
  if (absl::StartsWithIgnoreCase(s, "0x")) {
    s = s.substr(2);
    CHECK(absl::SimpleHexAtoi(s, &v)) << "invalid hex address: " << s;
  } else {
    CHECK(absl::SimpleAtoi(s, &v)) << "invalid decimal address: " << s;
  }
  return reinterpret_cast<void*>(v);
}

uint64_t ParseXferSize() {
  const uint64_t v = absl::GetFlag(FLAGS_xfer_size);
  return std::max(static_cast<uint64_t>(1), v);
}

uint32_t ParseNumXfers() {
  const uint32_t v = absl::GetFlag(FLAGS_num_xfers);
  return std::max(1U, v);
}

}  // namespace peregrine::benchmark
