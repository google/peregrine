"""Unit tests for report.py."""

from absl.testing import absltest
from peregrine.test.cluster.orchestrator import report


class ReportTest(absltest.TestCase):

  def test_parse_prefixed_json(self):
    line = 'PEREGRINE_NODE_READY {"targets":["127.0.0.1:10000@12345"]}\n'
    parsed = report.parse_prefixed_json(line, report.NODE_READY_PREFIX)
    self.assertIsNotNone(parsed)
    self.assertEqual(parsed["targets"], ["127.0.0.1:10000@12345"])
    self.assertIsNone(
        report.parse_prefixed_json(line, report.WORKLOAD_DONE_PREFIX)
    )

  def test_aggregate_and_format_summary_table(self):
    buckets = [0] * 32
    buckets[14] = 1000  # [8192, 16384) us
    sender_snap = {
        "num_instances": 1000,
        "xfer_size_bytes": 1,
        "cpu": {
            "wall_ms": 40.0,
            "user_cpu_ms": 80.0,
            "sys_cpu_ms": 40.0,
            "avg_cores": 3.0,
        },
        "transport": {
            "write": {
                "bytes": 1000,
                "errors": 0,
                "e2e_latency_us": {
                    "sum": 16500000,
                    "count": 1000,
                    "buckets": buckets,
                },
                "request_size_bytes": {
                    "sum": 1000,
                    "count": 1000,
                    "buckets": [0] * 32,
                },
            },
            "read": {
                "bytes": 0,
                "errors": 0,
                "e2e_latency_us": {"sum": 0, "count": 0, "buckets": [0] * 32},
                "request_size_bytes": {
                    "sum": 0,
                    "count": 0,
                    "buckets": [0] * 32,
                },
            },
            "tcp_connect_failures": 0,
            "rpc_requests_received": 0,
        },
    }
    receiver_snap = {
        "num_instances": 1,
        "xfer_size_bytes": 1,
        "cpu": {
            "wall_ms": 45.0,
            "user_cpu_ms": 10.0,
            "sys_cpu_ms": 20.0,
            "avg_cores": 0.67,
        },
        "transport": {
            "write": {"bytes": 0, "errors": 0},
            "read": {"bytes": 0, "errors": 0},
            "tcp_connect_failures": 0,
            "rpc_requests_received": 1000,
        },
    }
    agg = report.aggregate_cluster_results(
        config_summary={
            "num_client_nodes": 1,
            "clients_per_node": 1000,
            "num_server_nodes": 1,
            "servers_per_node": 1,
            "traffic_pattern": "all_to_all",
            "workload": "serial_fixed_write",
            "conn": 1,
            "num_xfers": 1,
        },
        sender_workload_snapshots=[sender_snap],
        sender_final_snapshots=[sender_snap],
        receiver_final_snapshots=[receiver_snap],
    )
    self.assertEqual(agg["latency"]["count"], 1000)
    self.assertAlmostEqual(agg["latency"]["mean_ms"], 16.5)
    self.assertAlmostEqual(agg["latency"]["p50_ms"], 12.288)
    self.assertEqual(agg["receiver_transport"]["rpc_requests_received"], 1000)
    table = report.format_summary_table(agg)
    self.assertIn("Completed Xfers   : 1000 (errors: 0)", table)
    self.assertIn("Elapsed Time      : 40.000 ms", table)
    self.assertIn("  Write: Count 1000, Mean 16.500 ms", table)
    self.assertIn("  p50/90/99/99.9 : 12.288/15.565/16.302/16.376 ms", table)
    self.assertIn("  Read: Count 0, Mean 0.000 ms", table)
    self.assertIn("  p50/90/99/99.9 : 0.000/0.000/0.000/0.000 ms", table)
    self.assertIn("rpc_requests_received=1000", table)


if __name__ == "__main__":
  absltest.main()
