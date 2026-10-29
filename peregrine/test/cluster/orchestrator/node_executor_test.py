"""Unit tests for node_executor.py."""

import io
import os
import socket
import tarfile
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

  def test_cleanup_kills_remote_process_and_removes_binary(self):
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
    ):
      executor.stage_binary(["remote-host-1"], tmp_path)

    with mock.patch.object(node_executor.subprocess, "run") as mock_run:
      executor.cleanup()
      mock_run.assert_called_once()
      called_argv = mock_run.call_args[0][0]
      self.assertEqual(called_argv[:4], ["ssh", "-p", "2222", "remote-host-1"])
      self.assertEqual(
          called_argv[4],
          "pkill -9 -f '^/tmp/peregrine_node_test1234( |$)' 2>/dev/null ||"
          " true; rm -f /tmp/peregrine_node_test1234",
      )

  def test_collect_traces_copies_local_and_cleanup_removes_staging_dir(self):
    executor = node_executor.NodeExecutor(session_id="local123")
    staging_dir = executor.prepare_trace_dir("localhost")
    os.makedirs(staging_dir, exist_ok=True)
    with open(
        os.path.join(staging_dir, "node_0_inst_0.json"), "w", encoding="utf-8"
    ) as f:
      f.write('{"traceEvents":[]}')
    with open(
        os.path.join(staging_dir, "ignored.txt"), "w", encoding="utf-8"
    ) as f:
      f.write("ignore")

    out_traces = os.path.join(self.create_tempdir().full_path, "traces")
    collected = executor.collect_traces(
        ["localhost", "127.0.0.1"], out_traces
    )
    expected_dst = os.path.join(out_traces, "node_0_inst_0.json")
    self.assertEqual(collected, [expected_dst])
    self.assertFalse(os.path.exists(os.path.join(out_traces, "ignored.txt")))

    executor.cleanup()
    self.assertFalse(os.path.exists(staging_dir))
    self.assertTrue(os.path.isfile(expected_dst))

  def test_collect_traces_streams_tar_gz_from_remote_and_cleans_up(self):
    executor = node_executor.NodeExecutor(
        ssh_cmd="ssh -p 2222", remote_dir="/tmp", session_id="test1234"
    )
    remote_dir = executor.prepare_trace_dir("remote-host-1")
    self.assertEqual(remote_dir, "/tmp/peregrine_traces_test1234")

    tar_buf = io.BytesIO()
    with tarfile.open(fileobj=tar_buf, mode="w:gz") as tf:
      payload = b'{"traceEvents":[{"ph":"X"}]}'
      info = tarfile.TarInfo(name="./node_0_inst_0.json")
      info.size = len(payload)
      tf.addfile(info, io.BytesIO(payload))

    completed = mock.MagicMock()
    completed.returncode = 0
    completed.stdout = tar_buf.getvalue()
    completed.stderr = b""

    out_traces = os.path.join(self.create_tempdir().full_path, "traces")
    with mock.patch.object(
        node_executor.subprocess, "run", return_value=completed
    ) as mock_run:
      collected = executor.collect_traces(["remote-host-1"], out_traces)
      expected_dst = os.path.join(out_traces, "node_0_inst_0.json")
      self.assertEqual(collected, [expected_dst])
      with open(expected_dst, "rb") as f:
        self.assertEqual(f.read(), b'{"traceEvents":[{"ph":"X"}]}')
      called_argv = mock_run.call_args[0][0]
      self.assertEqual(called_argv[:4], ["ssh", "-p", "2222", "remote-host-1"])
      self.assertIn(
          "tar -czf - -C /tmp/peregrine_traces_test1234 .", called_argv[4]
      )

    with mock.patch.object(node_executor.subprocess, "run") as mock_cleanup:
      executor.cleanup()
      mock_cleanup.assert_called_once()
      cleanup_argv = mock_cleanup.call_args[0][0]
      self.assertEqual(
          cleanup_argv[4], "rm -rf /tmp/peregrine_traces_test1234"
      )


if __name__ == "__main__":
  absltest.main()
