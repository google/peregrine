"""Unit tests for preflight.py."""

from absl.testing import absltest
from peregrine.test.cluster.orchestrator import preflight


class PreflightTest(absltest.TestCase):

  def test_compute_requirements_fan_in_1000(self):
    req = preflight.compute_requirements(
        num_client_nodes=1,
        clients_per_node=1000,
        num_server_nodes=1,
        servers_per_node=1,
        conn=1,
        traffic_pattern="all_to_all",
        colocated=True,
    )
    self.assertGreaterEqual(req.somaxconn, 1000)
    self.assertGreaterEqual(req.tcp_max_syn_backlog, 1000)
    self.assertGreaterEqual(req.ulimit_nofile, 20000)

  def test_parse_and_evaluate_node_passes_when_limits_sufficient(self):
    req = preflight.compute_requirements(
        num_client_nodes=1,
        clients_per_node=1000,
        num_server_nodes=1,
        servers_per_node=1,
        conn=1,
        traffic_pattern="all_to_all",
        colocated=True,
    )
    probe_out = "\n".join([
        "4096",
        "4096",
        "NA",
        "32768 60999",
        "65530",
        "2054695",
        "1048576",
    ])
    info = preflight.parse_probe_output("node-a", probe_out)
    self.assertIsNone(info.netdev_max_backlog)
    self.assertEqual(preflight.evaluate_node(info, req), [])

  def test_verify_nodes_warns_without_raising_on_insufficient_somaxconn(self):
    req = preflight.compute_requirements(
        num_client_nodes=1,
        clients_per_node=2000,
        num_server_nodes=1,
        servers_per_node=1,
        conn=1,
    )
    bad_info = preflight.NodeSysInfo(
        host="node-low",
        somaxconn=128,
        tcp_max_syn_backlog=4096,
        netdev_max_backlog=2000,
        ephemeral_port_low=10000,
        ephemeral_port_high=65000,
        vm_max_map_count=262144,
        kernel_threads_max=262144,
        ulimit_nofile_hard=1048576,
    )
    with self.assertLogs(level="WARNING") as cm:
      infos = preflight.verify_nodes(["node-low"], req, lambda _: bad_info)
    self.assertLen(infos, 1)
    self.assertIn("net.core.somaxconn", "\n".join(cm.output))

  def test_inspect_local_node_succeeds(self):
    info = preflight.inspect_local_node("localhost")
    self.assertGreater(info.somaxconn, 0)
    self.assertGreater(info.ephemeral_ports, 0)


if __name__ == "__main__":
  absltest.main()
