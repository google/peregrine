"""Preflight kernel sysctl and ulimit validator for Peregrine cluster tests."""

from collections.abc import Callable, Sequence
from concurrent import futures
import dataclasses
import math
import os
import resource

from absl import logging


@dataclasses.dataclass(frozen=True)
class NodeRequirements:
  """Minimum OS kernel and resource limits required on each cluster node."""

  somaxconn: int
  tcp_max_syn_backlog: int
  netdev_max_backlog: int
  ephemeral_ports: int
  vm_max_map_count: int
  kernel_threads_max: int
  ulimit_nofile: int


@dataclasses.dataclass(frozen=True)
class NodeSysInfo:
  """Observed OS kernel and resource limits on a single node."""

  host: str
  somaxconn: int
  tcp_max_syn_backlog: int
  netdev_max_backlog: int | None
  ephemeral_port_low: int
  ephemeral_port_high: int
  vm_max_map_count: int
  kernel_threads_max: int
  ulimit_nofile_hard: int

  @property
  def ephemeral_ports(self) -> int:
    """Returns the total number of available ephemeral ports."""
    return max(0, self.ephemeral_port_high - self.ephemeral_port_low + 1)


class Error(RuntimeError):
  """Raised when preflight probing or parsing fails on a cluster node."""


def compute_requirements(
    num_client_nodes: int,
    clients_per_node: int,
    num_server_nodes: int,
    servers_per_node: int,
    conn: int,
    traffic_pattern: str = "all_to_all",
    colocated: bool = False,
    min_somaxconn: int = 0,
) -> NodeRequirements:
  """Computes minimum required kernel sysctl and ulimit thresholds."""
  total_clients = max(1, num_client_nodes * clients_per_node)
  total_servers = max(1, num_server_nodes * servers_per_node)
  conn = max(1, conn)

  if traffic_pattern.lower() == "round_robin":
    max_clients_per_server = math.ceil(total_clients / total_servers)
    targets_per_client = 1
  else:
    max_clients_per_server = total_clients
    targets_per_client = total_servers

  c_in = max_clients_per_server * conn
  c_out = clients_per_node * targets_per_client * (conn + 1)
  p_node = (
      (clients_per_node + servers_per_node)
      if colocated
      else max(clients_per_node, servers_per_node)
  )

  somaxconn = max(1024, c_in, min_somaxconn)
  tcp_max_syn_backlog = max(1024, c_in, min_somaxconn)
  netdev_max_backlog = max(1000, c_in)
  ephemeral_ports = 2 * p_node + c_out + 1024
  vm_max_map_count = 20 * p_node + 4 * c_in
  kernel_threads_max = 20 * p_node + 4 * c_in
  ulimit_nofile = 16 * p_node + 4 * (c_in + c_out) + 1024

  return NodeRequirements(
      somaxconn=somaxconn,
      tcp_max_syn_backlog=tcp_max_syn_backlog,
      netdev_max_backlog=netdev_max_backlog,
      ephemeral_ports=ephemeral_ports,
      vm_max_map_count=vm_max_map_count,
      kernel_threads_max=kernel_threads_max,
      ulimit_nofile=ulimit_nofile,
  )


def _read_local_proc_int(path: str) -> int | None:
  """Reads an integer sysctl value from `/proc`, or None if unavailable."""
  try:
    with open(path, "r", encoding="utf-8") as f:
      return int(f.read().strip())
  except OSError:
    return None


def inspect_local_node(host: str = "localhost") -> NodeSysInfo:
  """Reads sysctl and RLIMIT_NOFILE values directly from the local machine."""
  somaxconn = _read_local_proc_int("/proc/sys/net/core/somaxconn") or 0
  syn_backlog = (
      _read_local_proc_int("/proc/sys/net/ipv4/tcp_max_syn_backlog") or 0
  )
  netdev_backlog = _read_local_proc_int("/proc/sys/net/core/netdev_max_backlog")
  vm_map_count = _read_local_proc_int("/proc/sys/vm/max_map_count") or 0
  threads_max = _read_local_proc_int("/proc/sys/kernel/threads-max") or 0

  port_low, port_high = 32768, 60999
  if os.path.exists("/proc/sys/net/ipv4/ip_local_port_range"):
    with open(
        "/proc/sys/net/ipv4/ip_local_port_range", "r", encoding="utf-8"
    ) as f:
      parts = f.read().split()
      if len(parts) >= 2:
        port_low, port_high = int(parts[0]), int(parts[1])

  _, hard_nofile = resource.getrlimit(resource.RLIMIT_NOFILE)
  if hard_nofile == resource.RLIM_INFINITY or hard_nofile < 0:
    hard_nofile = 1048576

  return NodeSysInfo(
      host=host,
      somaxconn=somaxconn,
      tcp_max_syn_backlog=syn_backlog,
      netdev_max_backlog=netdev_backlog,
      ephemeral_port_low=port_low,
      ephemeral_port_high=port_high,
      vm_max_map_count=vm_map_count,
      kernel_threads_max=threads_max,
      ulimit_nofile_hard=hard_nofile,
  )


REMOTE_PROBE_SCRIPT = (
    "cat /proc/sys/net/core/somaxconn; "
    "cat /proc/sys/net/ipv4/tcp_max_syn_backlog; "
    "if [ -f /proc/sys/net/core/netdev_max_backlog ]; then "
    "cat /proc/sys/net/core/netdev_max_backlog; else echo NA; fi; "
    "cat /proc/sys/net/ipv4/ip_local_port_range; "
    "cat /proc/sys/vm/max_map_count; "
    "cat /proc/sys/kernel/threads-max; "
    "ulimit -Hn"
)


def parse_probe_output(host: str, stdout: str) -> NodeSysInfo:
  """Parses the 7-line output of REMOTE_PROBE_SCRIPT into NodeSysInfo."""
  lines = [line.strip() for line in stdout.strip().splitlines() if line.strip()]
  if len(lines) < 7:
    raise Error(
        f"Incomplete preflight probe output from node '{host}': {stdout!r}"
    )
  somaxconn = int(lines[0])
  syn_backlog = int(lines[1])
  netdev_backlog = None if lines[2] == "NA" else int(lines[2])
  port_parts = lines[3].split()
  port_low, port_high = int(port_parts[0]), int(port_parts[1])
  vm_map_count = int(lines[4])
  threads_max = int(lines[5])
  ulimit_raw = lines[6]
  ulimit_hard = (
      1048576 if ulimit_raw.lower() == "unlimited" else int(ulimit_raw)
  )
  return NodeSysInfo(
      host=host,
      somaxconn=somaxconn,
      tcp_max_syn_backlog=syn_backlog,
      netdev_max_backlog=netdev_backlog,
      ephemeral_port_low=port_low,
      ephemeral_port_high=port_high,
      vm_max_map_count=vm_map_count,
      kernel_threads_max=threads_max,
      ulimit_nofile_hard=ulimit_hard,
  )


@dataclasses.dataclass(frozen=True)
class _CheckSpec:
  param_name: str
  get_actual: Callable[[NodeSysInfo], int | None]
  get_required: Callable[[NodeRequirements], int]
  format_remediation: Callable[[int], str]


_PREFLIGHT_CHECKS: tuple[_CheckSpec, ...] = (
    _CheckSpec(
        param_name="net.core.somaxconn",
        get_actual=lambda i: i.somaxconn,
        get_required=lambda r: r.somaxconn,
        format_remediation=lambda req: (
            f"sudo sysctl -w net.core.somaxconn={max(8192, req)}"
        ),
    ),
    _CheckSpec(
        param_name="net.ipv4.tcp_max_syn_backlog",
        get_actual=lambda i: i.tcp_max_syn_backlog,
        get_required=lambda r: r.tcp_max_syn_backlog,
        format_remediation=lambda req: (
            f"sudo sysctl -w net.ipv4.tcp_max_syn_backlog={max(8192, req)}"
        ),
    ),
    _CheckSpec(
        param_name="net.core.netdev_max_backlog",
        get_actual=lambda i: i.netdev_max_backlog,
        get_required=lambda r: r.netdev_max_backlog,
        format_remediation=lambda req: (
            f"sudo sysctl -w net.core.netdev_max_backlog={max(16384, req)}"
        ),
    ),
    _CheckSpec(
        param_name="net.ipv4.ip_local_port_range (width)",
        get_actual=lambda i: i.ephemeral_ports,
        get_required=lambda r: r.ephemeral_ports,
        format_remediation=lambda _: (
            'sudo sysctl -w net.ipv4.ip_local_port_range="10000 65535"'
        ),
    ),
    _CheckSpec(
        param_name="vm.max_map_count",
        get_actual=lambda i: i.vm_max_map_count,
        get_required=lambda r: r.vm_max_map_count,
        format_remediation=lambda req: (
            f"sudo sysctl -w vm.max_map_count={max(262144, req)}"
        ),
    ),
    _CheckSpec(
        param_name="kernel.threads-max",
        get_actual=lambda i: i.kernel_threads_max,
        get_required=lambda r: r.kernel_threads_max,
        format_remediation=lambda req: (
            f"sudo sysctl -w kernel.threads-max={max(131072, req)}"
        ),
    ),
    _CheckSpec(
        param_name="ulimit -Hn (RLIMIT_NOFILE)",
        get_actual=lambda i: i.ulimit_nofile_hard,
        get_required=lambda r: r.ulimit_nofile,
        format_remediation=lambda req: (
            f"Raise hard nofile limit >= {req} in /etc/security/limits.conf"
        ),
    ),
)


def evaluate_node(
    info: NodeSysInfo, req: NodeRequirements
) -> list[tuple[str, str, int, int, str]]:
  """Returns a list of (host, param, actual, required, remediation) failures."""
  failures: list[tuple[str, str, int, int, str]] = []
  for spec in _PREFLIGHT_CHECKS:
    actual = spec.get_actual(info)
    required = spec.get_required(req)
    if actual is not None and actual < required:
      failures.append((
          info.host,
          spec.param_name,
          actual,
          required,
          spec.format_remediation(required),
      ))
  return failures


def format_failures_warning(
    failures: Sequence[tuple[str, str, int, int, str]],
) -> str:
  """Formats preflight threshold mismatches into a human-readable warning table."""
  if not failures:
    return ""
  lines = [
      (
          "Preflight kernel/resource check found sub-optimal limits on one or"
          " more nodes (continuing anyway):"
      ),
      (
          f"  {'NODE':<20} {'PARAMETER':<36} {'ACTUAL':>10}"
          f" {'RECOMMENDED':>11}  REMEDIATION"
      ),
      "  " + "-" * 106,
  ]
  for host, param, actual, required, remediation in failures:
    lines.append(
        f"  {host:<20} {param:<36} {actual:>10} {required:>11}  {remediation}"
    )
  return "\n".join(lines)


def verify_nodes(
    hosts: Sequence[str],
    requirements: NodeRequirements,
    inspect_fn: Callable[[str], NodeSysInfo],
) -> list[NodeSysInfo]:
  """Inspects all unique hosts in parallel and logs warnings on sub-optimal limits."""
  unique_hosts = list(dict.fromkeys(hosts))
  infos: list[NodeSysInfo] = []
  with futures.ThreadPoolExecutor(
      max_workers=max(1, min(16, len(unique_hosts)))
  ) as pool:
    future_map = {h: pool.submit(inspect_fn, h) for h in unique_hosts}
    for h in unique_hosts:
      try:
        infos.append(future_map[h].result())
      except Exception as e:  # pylint: disable=broad-exception-caught
        logging.warning(
            "Preflight probe failed on node '%s' (continuing anyway): %s", h, e
        )

  all_failures: list[tuple[str, str, int, int, str]] = []
  for info in infos:
    all_failures.extend(evaluate_node(info, requirements))

  if all_failures:
    logging.warning("%s", format_failures_warning(all_failures))

  return infos
