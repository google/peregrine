"""End-to-end localhost integration test for bipartite_runner / cluster_session."""

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


if __name__ == "__main__":
  absltest.main()
