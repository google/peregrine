"""End-to-end localhost integration test for bipartite_runner / cluster_session."""

import json
import os
import socket

from absl.testing import absltest

from peregrine.src.util import util
from peregrine.test.cluster.orchestrator import cluster_session


class BipartiteRunnerTest(absltest.TestCase):

  def test_localhost_fan_in_and_round_robin(self):
    server_port = util.find_free_port(socket.AF_INET, tcp=True)
    client_port = util.find_free_port(socket.AF_INET, tcp=True)
    if abs(client_port - server_port) < 50:
      client_port = (server_port + 200) if server_port < 60000 else 15000

    cfg = cluster_session.BipartiteSessionConfig(
        client_nodes=["localhost"],
        server_nodes=["localhost"],
        clients_per_node=8,
        servers_per_node=1,
        server_base_port=server_port,
        client_base_port=client_port,
        transport="tcp",
        conn=1,
        workload="serial_fixed_write",
        xfer_size=1,
        num_xfers=1,
        traffic_pattern="all_to_all",
        cpu_affinity="none",
    )
    results = cluster_session.run_bipartite_session(cfg)
    self.assertEqual(results["latency"]["count"], 8)
    self.assertEqual(results["latency"]["errors"], 0)
    self.assertEqual(results["receiver_transport"]["rpc_requests_received"], 8)

  def test_localhost_trace_collection_and_results_json(self):
    server_port = util.find_free_port(socket.AF_INET, tcp=True)
    client_port = util.find_free_port(socket.AF_INET, tcp=True)
    if abs(client_port - server_port) < 50:
      client_port = (server_port + 200) if server_port < 60000 else 15000

    out_dir = os.path.join(self.create_tempdir().full_path, "run_artifacts")
    cfg = cluster_session.BipartiteSessionConfig(
        client_nodes=["localhost"],
        server_nodes=["localhost"],
        clients_per_node=2,
        servers_per_node=1,
        server_base_port=server_port,
        client_base_port=client_port,
        transport="tcp",
        conn=1,
        workload="serial_fixed_write",
        xfer_size=1,
        num_xfers=2,
        traffic_pattern="all_to_all",
        cpu_affinity="none",
        output_dir=out_dir,
        trace_every_n=1,
    )
    results = cluster_session.run_bipartite_session(cfg)
    self.assertEqual(results["latency"]["count"], 4)
    self.assertEqual(results["config"]["trace_every_n"], 1)

    results_path = os.path.join(out_dir, "results.json")
    self.assertTrue(os.path.isfile(results_path))
    with open(results_path, "r", encoding="utf-8") as f:
      saved = json.load(f)
    self.assertEqual(saved["latency"]["count"], 4)

    traces_dir = os.path.join(out_dir, "traces")
    self.assertEqual(
        sorted(os.listdir(traces_dir)),
        ["node_0_inst_0.json", "node_0_inst_1.json"],
    )
    for name in ("node_0_inst_0.json", "node_0_inst_1.json"):
      with open(os.path.join(traces_dir, name), "r", encoding="utf-8") as f:
        trace_doc = json.load(f)
      slices = [
          ev for ev in trace_doc["traceEvents"] if ev.get("ph") == "X"
      ]
      self.assertNotEmpty(slices)

  def test_trace_every_n_without_output_dir_raises(self):
    cfg = cluster_session.BipartiteSessionConfig(
        client_nodes=["localhost"],
        server_nodes=["localhost"],
        trace_every_n=1,
    )
    with self.assertRaisesRegex(ValueError, "requires output_dir"):
      cluster_session.run_bipartite_session(cfg)


if __name__ == "__main__":
  absltest.main()
