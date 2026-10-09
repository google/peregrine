#include "peregrine/test/cluster/node/flags.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/test/cluster/node/affinity.h"
#include "peregrine/test/cluster/node/config.h"
#include "peregrine/test/cluster/node/topology.h"
#include "peregrine/test/workloads/workload_generator.h"

ABSL_FLAG(std::string, ip, "127.0.0.1",
          "Local IP address to bind Peregrine instances to (supports loopback "
          "127.0.0.1 / ::1 and physical NIC IPs).");

ABSL_FLAG(int, num_instances, 1,
          "Number of Peregrine Transport instances to run in this process.");

ABSL_FLAG(int, node_index, 0,
          "0-based index of this node within its sender group.");

ABSL_FLAG(uint16_t, base_control_port, 10000,
          "Starting port for deterministic Peregrine gRPC control plane port "
          "assignment.");

ABSL_FLAG(std::string, transport, "tcp",
          "Transport type to use ('tcp' or 'rdma').");

ABSL_FLAG(int, conn, 1,
          "Number of data channels per peer (num_conns_per_peer).");

ABSL_FLAG(std::string, workload, "serial_fixed_write",
          "Workload generator ('serial_fixed_write' or 'kv_cache').");

ABSL_FLAG(std::string, targets, "",
          "Comma-separated list of target instances in the form "
          "'<ip>:<peregrine_port>@<raddr_uint64>' (empty for passive server "
          "nodes).");

ABSL_FLAG(std::string, traffic_pattern, "all_to_all",
          "Traffic routing pattern ('all_to_all' or 'round_robin').");

ABSL_FLAG(bool, exclude_self, false,
          "Exclude local instance's own endpoint when assigning targets.");

ABSL_FLAG(uint32_t, num_xfers, 1,
          "Number of transfer iterations per (local_instance, target) stream.");

ABSL_FLAG(bool, share_buffer, true,
          "Register one shared memory buffer across all instances in this "
          "process.");

ABSL_FLAG(std::string, cpu_affinity, "",
          "Optional CPU affinity policy ('none', 'numa:<node_id>', or explicit "
          "CPU list like '0-58,120-179').");

ABSL_FLAG(absl::Duration, metrics_interval, absl::Seconds(1),
          "Periodic sampling interval for TransportMetrics and CPU usage.");

namespace peregrine::cluster {
namespace {

absl::StatusOr<TransportType> ParseTransportFlag(std::string_view raw) {
  std::string_view s = absl::StripAsciiWhitespace(raw);
  if (absl::EqualsIgnoreCase(s, "tcp")) {
    return TransportType::kTcp;
  }
  if (absl::EqualsIgnoreCase(s, "rdma")) {
    return TransportType::kRdma;
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Invalid --transport '", raw, "': expected 'tcp' or 'rdma'"));
}

absl::StatusOr<workloads::WorkloadType> ParseWorkloadFlag(
    std::string_view raw) {
  std::string_view s = absl::StripAsciiWhitespace(raw);
  if (absl::EqualsIgnoreCase(s, "serial_fixed_write")) {
    return workloads::WorkloadType::kSerialFixedWrite;
  }
  if (absl::EqualsIgnoreCase(s, "kv_cache")) {
    return workloads::WorkloadType::kKvCache;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid --workload '", raw,
                   "': expected 'serial_fixed_write' or 'kv_cache'"));
}

}  // namespace

absl::StatusOr<std::string> ValidateIp(std::string_view ip) {
  ip = absl::StripAsciiWhitespace(ip);
  if (ip.empty()) {
    return absl::InvalidArgumentError("--ip must not be empty");
  }
  const auto parsed = util::IpAddr::Create(ip);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("--ip '", ip, "' is not a valid IPv4 or IPv6 address"));
  }
  if (parsed->IsZero()) {
    return absl::InvalidArgumentError(
        absl::StrCat("--ip cannot be a wildcard address ('", ip, "')"));
  }
  if (parsed->IsLoopback()) {
    return std::string(ip);
  }

  const auto nics = util::EnumerateNics();
  std::vector<std::string> available;
  for (const auto& [ifname, ips] : nics) {
    for (const auto& if_ip : ips) {
      available.push_back(absl::StrFormat("%s: %s", ifname, if_ip));
      if (if_ip == ip || util::IpAddr::Create(if_ip) == *parsed) {
        return std::string(ip);
      }
    }
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Specified --ip '", ip,
      "' does not match any local interface on this machine. Available:\n  ",
      absl::StrJoin(available, "\n  ")));
}

absl::StatusOr<NodeConfig> ReadNodeConfig() {
  NodeConfig cfg;

  auto ip_or = ValidateIp(absl::GetFlag(FLAGS_ip));
  if (!ip_or.ok()) return ip_or.status();
  cfg.ip = std::move(*ip_or);

  auto transport_or = ParseTransportFlag(absl::GetFlag(FLAGS_transport));
  if (!transport_or.ok()) return transport_or.status();
  cfg.transport_type = *transport_or;

  cfg.num_instances = absl::GetFlag(FLAGS_num_instances);
  if (cfg.num_instances <= 0) {
    return absl::InvalidArgumentError("--num_instances must be > 0");
  }

  cfg.node_index = std::max(0, absl::GetFlag(FLAGS_node_index));
  cfg.base_control_port = absl::GetFlag(FLAGS_base_control_port);
  cfg.num_conns = std::clamp(absl::GetFlag(FLAGS_conn), 1, 100);

  auto workload_or = ParseWorkloadFlag(absl::GetFlag(FLAGS_workload));
  if (!workload_or.ok()) return workload_or.status();
  cfg.workload = *workload_or;

  auto targets_or = ParseTargets(absl::GetFlag(FLAGS_targets));
  if (!targets_or.ok()) return targets_or.status();
  cfg.targets = std::move(*targets_or);

  auto pattern_or = ParseTrafficPattern(absl::GetFlag(FLAGS_traffic_pattern));
  if (!pattern_or.ok()) return pattern_or.status();
  cfg.traffic_pattern = *pattern_or;

  cfg.exclude_self = absl::GetFlag(FLAGS_exclude_self);
  cfg.num_xfers = std::max<uint32_t>(1, absl::GetFlag(FLAGS_num_xfers));
  cfg.share_buffer = absl::GetFlag(FLAGS_share_buffer);

  auto affinity_or = ValidateCpuAffinitySpec(absl::GetFlag(FLAGS_cpu_affinity));
  if (!affinity_or.ok()) return affinity_or.status();
  cfg.cpu_affinity = std::move(*affinity_or);

  cfg.metrics_interval = absl::GetFlag(FLAGS_metrics_interval);
  return cfg;
}

}  // namespace peregrine::cluster
