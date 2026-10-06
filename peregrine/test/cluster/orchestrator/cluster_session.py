"""Reusable multi-node 4-phase barrier session coordinator."""

from collections.abc import Mapping, Sequence
from concurrent import futures
import dataclasses
import subprocess
import threading
from typing import Any

from absl import logging

from peregrine.test.cluster.orchestrator import node_executor
from peregrine.test.cluster.orchestrator import preflight
from peregrine.test.cluster.orchestrator import report


@dataclasses.dataclass(frozen=True)
class BipartiteSessionConfig:
  """Configuration for a bipartite (N x N_p -> M x M_p) cluster run."""

  client_nodes: Sequence[str]
  server_nodes: Sequence[str]
  clients_per_node: int = 1
  servers_per_node: int = 1
  client_ips: Sequence[str] = ()
  server_ips: Sequence[str] = ()
  server_base_port: int = 10000
  client_base_port: int = 20000
  transport: str = "tcp"
  conn: int = 1
  workload: str = "serial_fixed_write"
  xfer_size: int = 1
  num_layers: int = 1
  num_blocks: int = 1
  block_size: int = 4096
  num_xfers: int = 1
  traffic_pattern: str = "all_to_all"
  share_buffer: bool = True
  node_minloglevel: int = 1
  metrics_interval: str = "1s"
  ssh_cmd: str = node_executor.DEFAULT_SSH_CMD
  remote_dir: str = "/tmp"
  node_binary: str = ""
  min_somaxconn: int = 0
  ready_timeout_sec: float = 120.0
  workload_timeout_sec: float = 600.0


class _ManagedNodeProcess:
  """Wraps a running `:node` subprocess and background stdout/stderr reader threads."""

  def __init__(self, label: str, host: str, proc: subprocess.Popen[str]):
    self.label = label
    self.host = host
    self.proc = proc
    self.ready_event = threading.Event()
    self.workload_done_event = threading.Event()
    self.node_done_event = threading.Event()
    self.ready_payload: dict[str, Any] | None = None
    self.workload_done_payload: dict[str, Any] | None = None
    self.node_done_payload: dict[str, Any] | None = None
    self.samples: list[dict[str, Any]] = []
    self.stderr_lines: list[str] = []
    self.error: str | None = None
    self._stderr_thread = threading.Thread(
        target=self._read_stderr_loop, daemon=True
    )
    self._stderr_thread.start()
    self._reader_thread = threading.Thread(
        target=self._read_stdout_loop, daemon=True
    )
    self._reader_thread.start()

  def _read_stderr_loop(self) -> None:
    """Reads stderr lines from the node subprocess in the background."""
    if self.proc.stderr is None:
      return
    try:
      for raw_line in self.proc.stderr:
        line = raw_line.rstrip()
        if line and len(self.stderr_lines) < 200:
          self.stderr_lines.append(line)
    except OSError:
      pass

  def _read_stdout_loop(self) -> None:
    """Parses protocol events and metrics samples from node stdout."""
    assert self.proc.stdout is not None
    for raw_line in self.proc.stdout:
      line = raw_line.strip()
      if not line:
        continue
      ready = report.parse_prefixed_json(line, report.NODE_READY_PREFIX)
      if ready is not None:
        self.ready_payload = ready
        self.ready_event.set()
        continue
      sample = report.parse_prefixed_json(line, report.METRICS_SAMPLE_PREFIX)
      if sample is not None:
        self.samples.append(sample)
        lat = report.summarize_node_snapshot(sample)
        if int(lat.get("count", 0)) > 0:
          logging.info(
              "  [Sample] %s (%s): completed=%d, errors=%d, throughput=%.4f"
              " Gbps",
              self.label,
              self.host,
              int(lat.get("count", 0)),
              int(lat.get("errors", 0)),
              float(lat.get("throughput_gbps", 0.0)),
          )
        continue
      w_done = report.parse_prefixed_json(line, report.WORKLOAD_DONE_PREFIX)
      if w_done is not None:
        self.workload_done_payload = w_done
        self.workload_done_event.set()
        continue
      n_done = report.parse_prefixed_json(line, report.NODE_DONE_PREFIX)
      if n_done is not None:
        self.node_done_payload = n_done
        self.node_done_event.set()
        continue

    rc = self.proc.poll()
    if not self.ready_event.is_set() or not self.node_done_event.is_set():
      stderr_tail = "\n".join(self.stderr_lines[-20:])
      self.error = (
          f"Node {self.label} ({self.host}) exited prematurely (rc={rc}):"
          f" {stderr_tail}"
      )
      self.ready_event.set()
      self.workload_done_event.set()
      self.node_done_event.set()

  def debug_summary(self) -> str:
    """Returns a diagnostic summary of recent samples and stderr lines."""
    last_sample = self.samples[-1] if self.samples else None
    stderr_tail = "\n  ".join(self.stderr_lines[-10:])
    return (
        f"{self.label} ({self.host}): samples={len(self.samples)},"
        f" last_sample={last_sample}, stderr_tail=[\n  {stderr_tail}\n]"
    )

  def send_command(self, cmd: str) -> None:
    """Writes a control command line (`START` or `STOP`) to node stdin."""
    if self.proc.stdin is not None and self.proc.poll() is None:
      try:
        self.proc.stdin.write(f"{cmd}\n")
        self.proc.stdin.flush()
      except OSError as e:
        logging.warning("Failed to send %s to %s: %s", cmd, self.label, e)

  def terminate(self) -> None:
    """Terminates or kills the node subprocess if still running."""
    if self.proc.poll() is None:
      self.proc.terminate()
      try:
        self.proc.wait(timeout=5)
      except subprocess.TimeoutExpired:
        self.proc.kill()


def _common_workload_flags(cfg: BipartiteSessionConfig) -> list[str]:
  """Builds common CLI flags shared by server and client `:node` processes."""
  flags = [
      f"--transport={cfg.transport}",
      f"--conn={cfg.conn}",
      f"--workload={cfg.workload}",
      f"--num_xfers={cfg.num_xfers}",
      f"--traffic_pattern={cfg.traffic_pattern}",
      f"--share_buffer={'true' if cfg.share_buffer else 'false'}",
      f"--minloglevel={cfg.node_minloglevel}",
      f"--metrics_interval={cfg.metrics_interval}",
  ]
  if cfg.workload.lower() == "serial_fixed_write":
    flags.append(f"--xfer_size={cfg.xfer_size}")
  elif cfg.workload.lower() == "kv_cache":
    flags.extend([
        f"--num_layers={cfg.num_layers}",
        f"--num_blocks={cfg.num_blocks}",
        f"--block_size={cfg.block_size}",
    ])
  return flags


def _spawn_node_group(
    role_prefix: str,
    hosts: Sequence[str],
    explicit_ips: Sequence[str],
    instances_per_node: int,
    base_port: int,
    common_flags: Sequence[str],
    extra_flags: Sequence[str],
    executor: node_executor.NodeExecutor,
    host_to_bin: Mapping[str, str],
    target_nofile: int,
) -> list[_ManagedNodeProcess]:
  """Spawns a group of server or client `:node` processes across `hosts`."""
  port_stride = max(100, instances_per_node * 2)
  procs: list[_ManagedNodeProcess] = []
  for idx, host in enumerate(hosts):
    explicit_ip = explicit_ips[idx] if idx < len(explicit_ips) else ""
    bind_ip = executor.resolve_bind_ip(host, explicit_ip)
    node_base_port = base_port + idx * port_stride
    args = [
        f"--ip={bind_ip}",
        f"--num_instances={instances_per_node}",
        f"--node_index={idx}",
        f"--base_control_port={node_base_port}",
        *extra_flags,
        *common_flags,
    ]
    logging.info(
        "  Spawning %s-%d on %s (bind_ip=%s, base_port=%d, instances=%d)",
        role_prefix,
        idx,
        host,
        bind_ip,
        node_base_port,
        instances_per_node,
    )
    proc = executor.spawn_node(
        host, host_to_bin[host], args, target_nofile=target_nofile
    )
    procs.append(_ManagedNodeProcess(f"{role_prefix}-{idx}", host, proc))
  return procs


def _wait_for_nodes_ready(
    procs: Sequence[_ManagedNodeProcess], timeout_sec: float
) -> list[str]:
  """Blocks until all `procs` emit `NODE_READY` and returns `ep@raddr` targets."""
  target_entries: list[str] = []
  for p in procs:
    if not p.ready_event.wait(timeout=timeout_sec):
      raise RuntimeError(f"Timed out waiting for {p.label} NODE_READY")
    if p.error or p.ready_payload is None:
      raise RuntimeError(p.error or f"{p.label} failed during init")
    targets = p.ready_payload["targets"]
    logging.info(
        "  %s (%s) ready: %d target(s)",
        p.label,
        p.host,
        len(targets),
    )
    target_entries.extend(targets)
  return target_entries


def _broadcast_command(procs: Sequence[_ManagedNodeProcess], cmd: str) -> None:
  """Broadcasts `cmd` concurrently across all `procs`."""
  if not procs:
    return
  with futures.ThreadPoolExecutor(max_workers=len(procs)) as pool:
    list(pool.map(lambda p: p.send_command(cmd), procs))


def _wait_for_workload_done(
    client_procs: Sequence[_ManagedNodeProcess],
    server_procs: Sequence[_ManagedNodeProcess],
    timeout_sec: float,
) -> None:
  """Waits for all active client nodes to emit `WORKLOAD_DONE`."""
  for cp in client_procs:
    if not cp.workload_done_event.wait(timeout=timeout_sec):
      srv_diag = "\n".join(sp.debug_summary() for sp in server_procs)
      raise RuntimeError(
          f"Timed out waiting for {cp.label} WORKLOAD_DONE.\n"
          f"Client state: {cp.debug_summary()}\n"
          f"Server state: {srv_diag}"
      )
    if cp.error or cp.workload_done_payload is None:
      raise RuntimeError(cp.error or f"{cp.label} failed during workload")
    lat = report.summarize_node_snapshot(cp.workload_done_payload)
    logging.info(
        "  %s (%s) finished workload: count=%d, errors=%d, elapsed=%.3f ms,"
        " throughput=%.4f Gbps",
        cp.label,
        cp.host,
        int(lat.get("count", 0)),
        int(lat.get("errors", 0)),
        float(lat.get("elapsed_ms", 0.0)),
        float(lat.get("throughput_gbps", 0.0)),
    )


def _stop_and_collect_nodes(
    all_procs: Sequence[_ManagedNodeProcess],
) -> None:
  """Broadcasts `STOP` and waits for all nodes to emit `NODE_DONE` and exit."""
  _broadcast_command(all_procs, "STOP")
  for p in all_procs:
    if not p.node_done_event.wait(timeout=30.0):
      raise RuntimeError(f"Timed out waiting for {p.label} NODE_DONE")
    if p.error or p.node_done_payload is None:
      raise RuntimeError(p.error or f"{p.label} failed during teardown")
    p.proc.wait(timeout=15.0)


def run_bipartite_session(cfg: BipartiteSessionConfig) -> dict[str, Any]:
  """Runs the full 4-phase bipartite benchmark session across all nodes."""
  all_hosts = [*cfg.server_nodes, *cfg.client_nodes]
  unique_hosts = list(dict.fromkeys(all_hosts))
  server_set = {
      "localhost" if node_executor.is_local_node(h) else h
      for h in cfg.server_nodes
  }
  client_set = {
      "localhost" if node_executor.is_local_node(h) else h
      for h in cfg.client_nodes
  }
  colocated = bool(server_set & client_set)

  requirements = preflight.compute_requirements(
      num_client_nodes=len(cfg.client_nodes),
      clients_per_node=cfg.clients_per_node,
      num_server_nodes=len(cfg.server_nodes),
      servers_per_node=cfg.servers_per_node,
      conn=cfg.conn,
      traffic_pattern=cfg.traffic_pattern,
      colocated=colocated,
      min_somaxconn=cfg.min_somaxconn,
  )

  executor = node_executor.NodeExecutor(
      ssh_cmd=cfg.ssh_cmd, remote_dir=cfg.remote_dir
  )
  server_procs: list[_ManagedNodeProcess] = []
  client_procs: list[_ManagedNodeProcess] = []

  try:
    # Step 1: Preflight check on all nodes (logs warnings on low limits).
    logging.info(
        "[Step 1/7] Running preflight checks on %d unique host(s): %s "
        "(recommended somaxconn>=%d, ulimit_nofile>=%d,"
        " ephemeral_ports>=%d)...",
        len(unique_hosts),
        ", ".join(unique_hosts),
        requirements.somaxconn,
        requirements.ulimit_nofile,
        requirements.ephemeral_ports,
    )
    preflight.verify_nodes(all_hosts, requirements, executor.inspect_node)
    logging.info(
        "[Step 1/7] Preflight check complete on %d host(s).",
        len(unique_hosts),
    )

    # Step 2: Locate `:node` binary and stage to all nodes.
    logging.info(
        "[Step 2/7] Locating :node binary and staging to %d host(s)...",
        len(unique_hosts),
    )
    local_bin = node_executor.locate_node_binary(explicit_path=cfg.node_binary)
    host_to_bin = executor.stage_binary(all_hosts, local_bin)
    logging.info(
        "[Step 2/7] Binary ready on all %d host(s).", len(unique_hosts)
    )

    target_nofile = max(65536, requirements.ulimit_nofile)
    common_flags = _common_workload_flags(cfg)

    # Step 3: Launch passive server nodes and await NODE_READY.
    logging.info(
        "[Step 3/7] Launching %d passive server node(s) (%d"
        " instance(s)/node)...",
        len(cfg.server_nodes),
        cfg.servers_per_node,
    )
    server_procs = _spawn_node_group(
        role_prefix="server",
        hosts=cfg.server_nodes,
        explicit_ips=cfg.server_ips,
        instances_per_node=cfg.servers_per_node,
        base_port=cfg.server_base_port,
        common_flags=common_flags,
        extra_flags=(),
        executor=executor,
        host_to_bin=host_to_bin,
        target_nofile=target_nofile,
    )
    target_entries = _wait_for_nodes_ready(server_procs, cfg.ready_timeout_sec)
    logging.info(
        "[Step 3/7] All %d server node(s) ready (%d total server endpoint(s)).",
        len(server_procs),
        len(target_entries),
    )

    # Step 4: Launch active client nodes with --targets and await NODE_READY.
    logging.info(
        "[Step 4/7] Launching %d active client node(s) (%d instance(s)/node,"
        " pattern=%s, targeting %d server endpoint(s))...",
        len(cfg.client_nodes),
        cfg.clients_per_node,
        cfg.traffic_pattern,
        len(target_entries),
    )
    client_procs = _spawn_node_group(
        role_prefix="client",
        hosts=cfg.client_nodes,
        explicit_ips=cfg.client_ips,
        instances_per_node=cfg.clients_per_node,
        base_port=cfg.client_base_port,
        common_flags=common_flags,
        extra_flags=(f"--targets={','.join(target_entries)}",),
        executor=executor,
        host_to_bin=host_to_bin,
        target_nofile=target_nofile,
    )
    _wait_for_nodes_ready(client_procs, cfg.ready_timeout_sec)
    logging.info("[Step 4/7] All %d client node(s) ready.", len(client_procs))

    # Step 5: Global START barrier across all server and client nodes.
    logging.info(
        "[Step 5/7] Releasing global START barrier across %d server(s) and %d"
        " client(s)...",
        len(server_procs),
        len(client_procs),
    )
    _broadcast_command(server_procs, "START")
    _broadcast_command(client_procs, "START")

    # Step 6: Wait for all active client nodes to emit WORKLOAD_DONE.
    logging.info(
        "[Step 6/7] Running workload (%s, num_xfers=%d) and waiting for"
        " WORKLOAD_DONE from %d client node(s) (timeout=%.1fs)...",
        cfg.workload,
        cfg.num_xfers,
        len(client_procs),
        cfg.workload_timeout_sec,
    )
    _wait_for_workload_done(
        client_procs, server_procs, cfg.workload_timeout_sec
    )
    logging.info(
        "[Step 6/7] All %d client node(s) completed workload.",
        len(client_procs),
    )

    # Step 7: Broadcast STOP to all nodes and collect NODE_DONE.
    logging.info(
        "[Step 7/7] Broadcasting STOP command and collecting final metrics from"
        " all %d node(s)...",
        len(client_procs) + len(server_procs),
    )
    _stop_and_collect_nodes([*client_procs, *server_procs])
    logging.info(
        "[Step 7/7] All nodes stopped cleanly. Aggregating cluster report."
    )

    config_summary: Mapping[str, Any] = {
        "num_client_nodes": len(cfg.client_nodes),
        "clients_per_node": cfg.clients_per_node,
        "num_server_nodes": len(cfg.server_nodes),
        "servers_per_node": cfg.servers_per_node,
        "traffic_pattern": cfg.traffic_pattern,
        "workload": cfg.workload,
        "conn": cfg.conn,
        "num_xfers": cfg.num_xfers,
    }
    return report.aggregate_cluster_results(
        config_summary=config_summary,
        sender_workload_snapshots=[
            cp.workload_done_payload
            for cp in client_procs
            if cp.workload_done_payload is not None
        ],
        sender_final_snapshots=[
            cp.node_done_payload
            for cp in client_procs
            if cp.node_done_payload is not None
        ],
        receiver_final_snapshots=[
            sp.node_done_payload
            for sp in server_procs
            if sp.node_done_payload is not None
        ],
    )
  finally:
    for p in [*client_procs, *server_procs]:
      p.terminate()
    executor.cleanup()
