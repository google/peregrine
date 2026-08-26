#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/util/nic.h"
#include "test/benchmark/types.h"

ABSL_FLAG(
    std::string, ip, "",
    "Local non-zero, non-loopback network interface IP address to bind to "
    "(e.g. 10.0.0.1)");

ABSL_FLAG(std::string, role, "receiver",
          "Role to run as: 'sender|send|s' or 'receiver|recv|r'");

ABSL_FLAG(
    uint16_t, app_control_port, 9999,
    "TCP port for the application control message exchange (listen port for "
    "receiver; target destination port for sender)");

ABSL_FLAG(
    uint16_t, peregrine_control_port, 9998,
    "TCP port for Peregrine's gRPC control plane (listen port for receiver; "
    "target destination port for sender; sender's local listener uses an "
    "ephemeral port)");

ABSL_FLAG(int, conn, 8,
          "#Connections to make between this process and each peer");

ABSL_FLAG(std::string, peer, "",
          "Receiver IP address or host name (required for sender)");

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

std::string ParseIp(std::string_view ip) {
  if (ip.empty()) {
    LOG(FATAL) << "--ip must be specified with a valid non-zero, non-loopback "
                  "network interface IP address.";
  }

  // Reject loopback and zero addresses.
  if (ip == "127.0.0.1" || ip == "::1" || ip == "0.0.0.0" || ip == "::" ||
      absl::StartsWith(ip, "127.")) {
    LOG(FATAL) << "--ip cannot be a loopback or wildcard IP address (" << ip
               << "). Please specify a valid physical network interface IP.";
  }

  // Validate at runtime that the provided IP matches one of the local NIC
  // interfaces.
  const auto nics = peregrine::util::EnumerateNics();
  bool found = false;
  std::vector<std::string> available_ips;
  for (const auto& [ifname, ips] : nics) {
    for (const auto& if_ip : ips) {
      available_ips.push_back(absl::StrFormat("%s: %s", ifname, if_ip));
      if (if_ip == ip) {
        found = true;
        break;
      }
    }
    if (found) break;
  }

  if (!found) {
    LOG(FATAL) << "Specified --ip '" << ip
               << "' does not match any local interface on this machine.\n"
               << "Available interfaces and IPs:\n  "
               << absl::StrJoin(available_ips, "\n  ");
  }

  return std::string(ip);
}

std::string ParseIp() { return ParseIp(absl::GetFlag(FLAGS_ip)); }

uint16_t ParseAppControlPort() { return absl::GetFlag(FLAGS_app_control_port); }

uint16_t ParsePeregrineControlPort() {
  return absl::GetFlag(FLAGS_peregrine_control_port);
}

int ParseNumConns() {
  const int v = absl::GetFlag(FLAGS_conn);
  return std::min(std::max(1, v), 100);
}

std::string ParsePeer() { return absl::GetFlag(FLAGS_peer); }

uint64_t ParseXferSize() {
  const uint64_t v = absl::GetFlag(FLAGS_xfer_size);
  return std::max(static_cast<uint64_t>(1), v);
}

uint32_t ParseNumXfers() {
  const uint32_t v = absl::GetFlag(FLAGS_num_xfers);
  return std::max(1U, v);
}

}  // namespace peregrine::benchmark
