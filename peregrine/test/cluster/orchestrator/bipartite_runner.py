"""CLI orchestrator for the Peregrine Cluster Bipartite Benchmark."""

from collections.abc import Sequence
import json

from absl import app
from absl import flags
from absl import logging

from peregrine.test.cluster.orchestrator import cluster_session
from peregrine.test.cluster.orchestrator import node_executor
from peregrine.test.cluster.orchestrator import report

_CLIENT_NODES = flags.DEFINE_list(
    "client_nodes",
    ["localhost"],
    "Comma-separated list of N client hostnames or IPs.",
)
_SERVER_NODES = flags.DEFINE_list(
    "server_nodes",
    ["localhost"],
    "Comma-separated list of M server hostnames or IPs.",
)
_CLIENTS_PER_NODE = flags.DEFINE_integer(
    "clients_per_node",
    1,
    "Number of Peregrine Transport instances per client node (N_p).",
)
_SERVERS_PER_NODE = flags.DEFINE_integer(
    "servers_per_node",
    1,
    "Number of Peregrine Transport instances per server node (M_p).",
)
_CLIENT_IPS = flags.DEFINE_list(
    "client_ips",
    [],
    "Optional explicit Peregrine bind IPs per client node.",
)
_SERVER_IPS = flags.DEFINE_list(
    "server_ips",
    [],
    "Optional explicit Peregrine bind IPs per server node.",
)
_SERVER_BASE_PORT = flags.DEFINE_integer(
    "server_base_port",
    10000,
    "Starting control plane port for server instances.",
)
_CLIENT_BASE_PORT = flags.DEFINE_integer(
    "client_base_port",
    20000,
    "Starting control plane port for client instances.",
)
_TRANSPORT = flags.DEFINE_string(
    "transport", "tcp", "Transport type ('tcp' or 'rdma')."
)
_CONN = flags.DEFINE_integer(
    "conn", 1, "Number of data channels per peer (num_conns_per_peer)."
)
_WORKLOAD = flags.DEFINE_string(
    "workload",
    "serial_fixed_write",
    "Workload generator ('serial_fixed_write' or 'kv_cache').",
)
_XFER_SIZE = flags.DEFINE_integer(
    "xfer_size", 1, "Transfer size in bytes (for serial_fixed_write)."
)
_NUM_LAYERS = flags.DEFINE_integer(
    "num_layers", 1, "Number of layers (for kv_cache)."
)
_NUM_BLOCKS = flags.DEFINE_integer(
    "num_blocks", 1, "Number of blocks (for kv_cache)."
)
_BLOCK_SIZE = flags.DEFINE_integer(
    "block_size", 4096, "Block size in bytes (for kv_cache)."
)
_NUM_XFERS = flags.DEFINE_integer(
    "num_xfers", 1, "Number of transfer iterations per stream."
)
_TRAFFIC_PATTERN = flags.DEFINE_string(
    "traffic_pattern",
    "all_to_all",
    "Traffic routing pattern ('all_to_all' or 'round_robin').",
)
_SHARE_BUFFER = flags.DEFINE_bool(
    "share_buffer",
    True,
    "Share one registered buffer across all instances in a node process.",
)
_CPU_AFFINITY = flags.DEFINE_string(
    "cpu_affinity",
    "",
    "Optional CPU affinity policy for :node processes ('none', 'numa:<node>',"
    " or explicit CPU list like '0-58,120-179').",
)
_NODE_MINLOGLEVEL = flags.DEFINE_integer(
    "node_minloglevel",
    1,
    "Minimum Abseil log level for :node processes (0=INFO, 1=WARNING,"
    " 2=ERROR, 3=FATAL).",
)
_METRICS_INTERVAL = flags.DEFINE_string(
    "metrics_interval",
    "1s",
    "Periodic sampling interval for node metrics.",
)
_SSH_CMD = flags.DEFINE_string(
    "ssh_cmd",
    node_executor.DEFAULT_SSH_CMD,
    "SSH command prefix for remote node staging and execution.",
)
_REMOTE_DIR = flags.DEFINE_string(
    "remote_dir", "/tmp", "Directory on remote nodes for staging binary."
)
_NODE_BINARY = flags.DEFINE_string(
    "node_binary", "", "Optional explicit path to pre-built :node binary."
)
_MIN_SOMAXCONN = flags.DEFINE_integer(
    "min_somaxconn",
    0,
    "Minimum required net.core.somaxconn and tcp_max_syn_backlog (0 = auto).",
)
_READY_TIMEOUT_SEC = flags.DEFINE_float(
    "ready_timeout_sec",
    120.0,
    "Timeout in seconds waiting for nodes to emit PEREGRINE_NODE_READY.",
)
_WORKLOAD_TIMEOUT_SEC = flags.DEFINE_float(
    "workload_timeout_sec",
    600.0,
    "Timeout in seconds waiting for active nodes to emit"
    " PEREGRINE_WORKLOAD_DONE.",
)
_OUTPUT_JSON = flags.DEFINE_string(
    "output_json", "", "Optional file path to write aggregated JSON results."
)


def main(argv: Sequence[str]) -> None:
  """Runs the Peregrine bipartite cluster benchmark CLI."""
  if len(argv) > 1:
    raise app.UsageError(f"Unrecognized positional arguments: {argv[1:]}")

  logging.set_stderrthreshold(logging.INFO)

  cfg = cluster_session.BipartiteSessionConfig(
      client_nodes=_CLIENT_NODES.value,
      server_nodes=_SERVER_NODES.value,
      clients_per_node=_CLIENTS_PER_NODE.value,
      servers_per_node=_SERVERS_PER_NODE.value,
      client_ips=_CLIENT_IPS.value,
      server_ips=_SERVER_IPS.value,
      server_base_port=_SERVER_BASE_PORT.value,
      client_base_port=_CLIENT_BASE_PORT.value,
      transport=_TRANSPORT.value,
      conn=_CONN.value,
      workload=_WORKLOAD.value,
      xfer_size=_XFER_SIZE.value,
      num_layers=_NUM_LAYERS.value,
      num_blocks=_NUM_BLOCKS.value,
      block_size=_BLOCK_SIZE.value,
      num_xfers=_NUM_XFERS.value,
      traffic_pattern=_TRAFFIC_PATTERN.value,
      share_buffer=_SHARE_BUFFER.value,
      cpu_affinity=_CPU_AFFINITY.value,
      node_minloglevel=_NODE_MINLOGLEVEL.value,
      metrics_interval=_METRICS_INTERVAL.value,
      ssh_cmd=_SSH_CMD.value,
      remote_dir=_REMOTE_DIR.value,
      node_binary=_NODE_BINARY.value,
      min_somaxconn=_MIN_SOMAXCONN.value,
      ready_timeout_sec=_READY_TIMEOUT_SEC.value,
      workload_timeout_sec=_WORKLOAD_TIMEOUT_SEC.value,
  )

  results = cluster_session.run_bipartite_session(cfg)

  print(report.format_summary_table(results))
  if _OUTPUT_JSON.value:
    with open(_OUTPUT_JSON.value, "w", encoding="utf-8") as f:
      json.dump(results, f, indent=2)
    logging.info("Wrote aggregated JSON report to %s", _OUTPUT_JSON.value)


if __name__ == "__main__":
  app.run(main)
