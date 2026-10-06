"""Unit tests for node_executor.py."""

import socket
import tempfile
from unittest import mock

from absl.testing import absltest

from peregrine.test.cluster.orchestrator import node_executor


class NodeExecutorTest(absltest.TestCase):

  def test_is_local_node(self):
    self.assertTrue(node_executor.is_local_node("localhost"))
    self.assertTrue(node_executor.is_local_node("127.0.0.1"))
    self.assertTrue(node_executor.is_local_node("::1"))
    self.assertTrue(node_executor.is_local_node(socket.gethostname()))
    self.assertFalse(node_executor.is_local_node("10.128.0.99"))

  def test_stage_binary_skips_ssh_on_localhost(self):
    executor = node_executor.NodeExecutor()
    mapping = executor.stage_binary(["localhost", "127.0.0.1"], "/bin/true")
    self.assertEqual(mapping["localhost"], "/bin/true")
    self.assertEqual(mapping["127.0.0.1"], "/bin/true")

  def test_stage_binary_streams_via_ssh_stdin_on_remote(self):
    with tempfile.NamedTemporaryFile(mode="wb", delete=False) as tmp:
      tmp.write(b"fake_elf_payload")
      tmp_path = tmp.name

    executor = node_executor.NodeExecutor(
        ssh_cmd="ssh -p 2222", remote_dir="/tmp", session_id="test1234"
    )
    fake_proc = mock.MagicMock()
    fake_proc.stdin = mock.MagicMock()
    fake_proc.communicate.return_value = (b"", b"")
    fake_proc.returncode = 0

    with mock.patch.object(
        node_executor.subprocess, "Popen", return_value=fake_proc
    ) as mock_popen:
      mapping = executor.stage_binary(["remote-host-1"], tmp_path)
      self.assertEqual(mapping["remote-host-1"], "/tmp/peregrine_node_test1234")
      called_argv = mock_popen.call_args[0][0]
      self.assertEqual(
          called_argv[:4],
          ["ssh", "-p", "2222", "remote-host-1"],
      )
      self.assertIn("cat > /tmp/peregrine_node_test1234", called_argv[4])

  def test_stage_binary_handles_copyfileobj_broken_pipe(self):
    with tempfile.NamedTemporaryFile(mode="wb", delete=False) as tmp:
      tmp.write(b"x" * (2 * 1024 * 1024))
      tmp_path = tmp.name

    executor = node_executor.NodeExecutor(
        ssh_cmd="bash -c 'echo Permission denied >&2; exit 255' --",
        remote_dir="/tmp",
        session_id="test1234",
    )
    with self.assertRaisesRegex(
        RuntimeError,
        r"Permission denied.*copyfileobj error",
    ):
      executor.stage_binary(["remote-host-1"], tmp_path)

  def test_build_ssh_cmd_supports_gcloud_compute_ssh(self):
    executor = node_executor.NodeExecutor(
        ssh_cmd="gcloud compute ssh --project=my-project --zone=us-central1-a"
    )
    self.assertEqual(
        executor._build_ssh_cmd("user@gce-vm-1", "echo ready"),
        [
            "gcloud",
            "compute",
            "ssh",
            "--project=my-project",
            "--zone=us-central1-a",
            "user@gce-vm-1",
            "--command=echo ready",
        ],
    )


if __name__ == "__main__":
  absltest.main()
