"""Multi-node JSON metrics parser and summary report generator."""

from collections.abc import Mapping, Sequence
import json
from typing import Any

NODE_READY_PREFIX = "PEREGRINE_NODE_READY "
METRICS_SAMPLE_PREFIX = "PEREGRINE_METRICS_SAMPLE "
WORKLOAD_DONE_PREFIX = "PEREGRINE_WORKLOAD_DONE "
NODE_DONE_PREFIX = "PEREGRINE_NODE_DONE "


def parse_prefixed_json(line: str, prefix: str) -> dict[str, Any] | None:
  """Extracts and parses the JSON payload if `line` starts with `prefix`."""
  stripped = line.strip()
  if not stripped.startswith(prefix):
    return None
  payload = stripped[len(prefix) :].strip()
  return json.loads(payload)


def _merge_histogram(dst: dict[str, Any], src: Mapping[str, Any]) -> None:
  """Accumulates `src` Log2Histogram counts and sum into `dst`."""
  dst["sum"] = dst.get("sum", 0) + int(src.get("sum", 0))
  dst["count"] = dst.get("count", 0) + int(src.get("count", 0))
  src_buckets = list(src.get("buckets", [0] * 32))
  dst_buckets = dst.setdefault("buckets", [0] * len(src_buckets))
  for i, val in enumerate(src_buckets):
    if i < len(dst_buckets):
      dst_buckets[i] += int(val)


def _merge_op_metrics(dst: dict[str, Any], src: Mapping[str, Any]) -> None:
  """Accumulates `src` operation bytes, errors, and histograms into `dst`."""
  dst["bytes"] = dst.get("bytes", 0) + int(src.get("bytes", 0))
  dst["errors"] = dst.get("errors", 0) + int(src.get("errors", 0))
  _merge_histogram(
      dst.setdefault("e2e_latency_us", {}), src.get("e2e_latency_us", {})
  )
  _merge_histogram(
      dst.setdefault("request_size_bytes", {}),
      src.get("request_size_bytes", {}),
  )


def merge_transport_dicts(
    snapshots: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
  """Merges the `transport` section across multiple node snapshots."""
  merged: dict[str, Any] = {
      "write": {
          "bytes": 0,
          "errors": 0,
          "e2e_latency_us": {"sum": 0, "count": 0, "buckets": [0] * 32},
          "request_size_bytes": {"sum": 0, "count": 0, "buckets": [0] * 32},
      },
      "read": {
          "bytes": 0,
          "errors": 0,
          "e2e_latency_us": {"sum": 0, "count": 0, "buckets": [0] * 32},
          "request_size_bytes": {"sum": 0, "count": 0, "buckets": [0] * 32},
      },
      "tcp_connect_failures": 0,
      "rpc_requests_received": 0,
  }
  for snap in snapshots:
    t = snap.get("transport", {})
    _merge_op_metrics(merged["write"], t.get("write", {}))
    _merge_op_metrics(merged["read"], t.get("read", {}))
    merged["tcp_connect_failures"] += int(t.get("tcp_connect_failures", 0))
    merged["rpc_requests_received"] += int(t.get("rpc_requests_received", 0))
  return merged


def _percentile_from_log2_histogram_ms(
    hist: Mapping[str, Any], q: float
) -> float:
  """Estimates the q-th quantile (in ms) from a Log2Histogram<32> (in us)."""
  buckets = [int(x) for x in hist.get("buckets", ())]
  total = sum(buckets)
  if total <= 0:
    return 0.0
  target = q * total
  cum = 0
  for i, count in enumerate(buckets):
    if count <= 0:
      continue
    prev_cum = cum
    cum += count
    if cum >= target:
      low_us = 0.0 if i == 0 else float(1 << (i - 1))
      high_us = 1.0 if i == 0 else float(1 << i)
      frac = max(0.0, min(1.0, (target - prev_cum) / count))
      return (low_us + frac * (high_us - low_us)) / 1000.0
  return 0.0


def _summarize_op_latency(op: Mapping[str, Any]) -> dict[str, Any]:
  """Computes count, mean_ms, and percentiles from an OpMetrics dict."""
  hist = op.get("e2e_latency_us", {})
  bucket_sum = sum(int(x) for x in hist.get("buckets", ()))
  count = int(hist.get("count", 0)) or bucket_sum
  if count > 0:
    mean_ms = (float(hist.get("sum", 0)) / count) / 1000.0
    p50_ms = _percentile_from_log2_histogram_ms(hist, 0.50)
    p90_ms = _percentile_from_log2_histogram_ms(hist, 0.90)
    p99_ms = _percentile_from_log2_histogram_ms(hist, 0.99)
    p999_ms = _percentile_from_log2_histogram_ms(hist, 0.999)
  else:
    mean_ms = p50_ms = p90_ms = p99_ms = p999_ms = 0.0
  return {
      "count": count,
      "mean_ms": mean_ms,
      "p50_ms": p50_ms,
      "p90_ms": p90_ms,
      "p99_ms": p99_ms,
      "p999_ms": p999_ms,
  }


def _aggregate_latency(
    active_snaps: Sequence[Mapping[str, Any]],
    sender_transport: Mapping[str, Any],
) -> dict[str, Any]:
  """Computes cluster-wide latency percentiles, elapsed time, and throughput."""
  write_op = sender_transport.get("write", {})
  read_op = sender_transport.get("read", {})
  write_lat = _summarize_op_latency(write_op)
  read_lat = _summarize_op_latency(read_op)

  combined_hist: dict[str, Any] = {"sum": 0, "count": 0, "buckets": [0] * 32}
  _merge_histogram(combined_hist, write_op.get("e2e_latency_us", {}))
  _merge_histogram(combined_hist, read_op.get("e2e_latency_us", {}))
  combined_lat = _summarize_op_latency({"e2e_latency_us": combined_hist})

  total_count = combined_lat["count"]
  total_errors = int(write_op.get("errors", 0)) + int(read_op.get("errors", 0))
  xfer_size_bytes = (
      max(int(s.get("xfer_size_bytes", 0)) for s in active_snaps)
      if active_snaps
      else 0
  )
  elapsed_ms = (
      max(float(s.get("cpu", {}).get("wall_ms", 0.0)) for s in active_snaps)
      if active_snaps
      else 0.0
  )

  transfers_per_sec = (
      (total_count / (elapsed_ms / 1000.0)) if elapsed_ms > 0.0 else 0.0
  )
  total_bytes = int(write_op.get("bytes", 0)) + int(read_op.get("bytes", 0))
  if total_bytes <= 0:
    total_bytes = total_count * xfer_size_bytes
  throughput_gbps = (
      (total_bytes * 8.0) / ((elapsed_ms / 1000.0) * 1e9)
      if elapsed_ms > 0.0
      else 0.0
  )

  return {
      "count": total_count,
      "errors": total_errors,
      "xfer_size_bytes": xfer_size_bytes,
      "p50_ms": combined_lat["p50_ms"],
      "p90_ms": combined_lat["p90_ms"],
      "p99_ms": combined_lat["p99_ms"],
      "p999_ms": combined_lat["p999_ms"],
      "mean_ms": combined_lat["mean_ms"],
      "write": write_lat,
      "read": read_lat,
      "elapsed_ms": elapsed_ms,
      "transfers_per_sec": transfers_per_sec,
      "throughput_gbps": throughput_gbps,
  }


def summarize_node_snapshot(snap: Mapping[str, Any]) -> dict[str, Any]:
  """Computes latency and throughput summary for a single node snapshot."""
  return _aggregate_latency([snap], merge_transport_dicts([snap]))


def aggregate_cluster_results(
    config_summary: Mapping[str, Any],
    sender_workload_snapshots: Sequence[Mapping[str, Any]],
    sender_final_snapshots: Sequence[Mapping[str, Any]],
    receiver_final_snapshots: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
  """Aggregates final metrics across all sender (client) and receiver (server) nodes."""
  active_snaps = (
      sender_workload_snapshots
      if sender_workload_snapshots
      else sender_final_snapshots
  )
  sender_transport = merge_transport_dicts(
      sender_final_snapshots or active_snaps
  )

  sender_cpu_cores = sum(
      float(s.get("cpu", {}).get("avg_cores", 0.0)) for s in active_snaps
  )
  receiver_cpu_cores = sum(
      float(s.get("cpu", {}).get("avg_cores", 0.0))
      for s in receiver_final_snapshots
  )

  return {
      "config": dict(config_summary),
      "latency": _aggregate_latency(active_snaps, sender_transport),
      "cpu": {
          "sender_total_avg_cores": sender_cpu_cores,
          "receiver_total_avg_cores": receiver_cpu_cores,
      },
      "sender_transport": sender_transport,
      "receiver_transport": merge_transport_dicts(receiver_final_snapshots),
  }


def format_summary_table(aggregated: Mapping[str, Any]) -> str:
  """Formats the aggregated cluster benchmark results into a readable table."""
  cfg = aggregated.get("config", {})
  lat = aggregated.get("latency", {})
  cpu = aggregated.get("cpu", {})
  st = aggregated.get("sender_transport", {})
  rt = aggregated.get("receiver_transport", {})
  write_lat = lat.get("write", _summarize_op_latency(st.get("write", {})))
  read_lat = lat.get("read", _summarize_op_latency(st.get("read", {})))

  lines = [
      "=" * 76,
      "Peregrine Cluster Bipartite Benchmark Results",
      "=" * 76,
      (
          f"Topology          : N={cfg.get('num_client_nodes', 1)} client"
          f" node(s) x N_p={cfg.get('clients_per_node', 1)} instances ->"
          f" M={cfg.get('num_server_nodes', 1)} server node(s) x"
          f" M_p={cfg.get('servers_per_node', 1)} instances"
      ),
      (
          "Traffic / Workload:"
          f" pattern={cfg.get('traffic_pattern', 'all_to_all')},"
          f" workload={cfg.get('workload', 'serial_fixed_write')},"
          f" conn={cfg.get('conn', 1)}, num_xfers={cfg.get('num_xfers', 1)}"
      ),
      f"Transfer Size     : {lat.get('xfer_size_bytes', 0)} B",
      "-" * 76,
      (
          f"Completed Xfers   : {lat.get('count', 0)}"
          f" (errors: {lat.get('errors', 0)})"
      ),
      f"Elapsed Time      : {lat.get('elapsed_ms', 0.0):.3f} ms",
      f"Transfer Rate     : {lat.get('transfers_per_sec', 0.0):.2f} xfers/s",
      f"Throughput        : {lat.get('throughput_gbps', 0.0):.4f} Gbps",
      "-" * 76,
      "End-to-End Post() Completion Latency (ms):",
      (
          f"  Write: Count {write_lat.get('count', 0)},"
          f" Mean {write_lat.get('mean_ms', 0.0):.3f} ms"
      ),
      (
          "  p50/90/99/99.9 :"
          f" {write_lat.get('p50_ms', 0.0):.3f}/{write_lat.get('p90_ms', 0.0):.3f}/"
          f"{write_lat.get('p99_ms', 0.0):.3f}/{write_lat.get('p999_ms', 0.0):.3f}"
          " ms"
      ),
      (
          f"  Read: Count {read_lat.get('count', 0)},"
          f" Mean {read_lat.get('mean_ms', 0.0):.3f} ms"
      ),
      (
          "  p50/90/99/99.9 :"
          f" {read_lat.get('p50_ms', 0.0):.3f}/{read_lat.get('p90_ms', 0.0):.3f}/"
          f"{read_lat.get('p99_ms', 0.0):.3f}/{read_lat.get('p999_ms', 0.0):.3f}"
          " ms"
      ),
      "-" * 76,
      "Transport & CPU Telemetry:",
      (
          "  Client Write    :"
          f" {st.get('write', {}).get('bytes', 0)} B,"
          f" errors={st.get('write', {}).get('errors', 0)},"
          f" tcp_connect_failures={st.get('tcp_connect_failures', 0)}"
      ),
  ]
  read_bytes = int(st.get("read", {}).get("bytes", 0))
  read_errors = int(st.get("read", {}).get("errors", 0))
  if read_bytes > 0 or read_errors > 0:
    lines.append(f"  Client Read     : {read_bytes} B, errors={read_errors}")
  lines.extend([
      (
          "  Server RPCs     :"
          f" rpc_requests_received={rt.get('rpc_requests_received', 0)},"
          f" tcp_connect_failures={rt.get('tcp_connect_failures', 0)}"
      ),
      (
          "  Avg CPU Cores   :"
          f" clients={cpu.get('sender_total_avg_cores', 0.0):.2f} cores,"
          f" servers={cpu.get('receiver_total_avg_cores', 0.0):.2f} cores"
      ),
      "=" * 76,
  ])
  return "\n".join(lines)
