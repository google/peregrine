"""Local subprocess and SSH-pipe node execution for Peregrine cluster tests."""

from collections.abc import Sequence
from concurrent import futures
import ipaddress
import os
import resource
import shlex
import shutil
import socket
import subprocess
import uuid

from absl import logging

from peregrine.test.cluster.orchestrator import preflight

DEFAULT_SSH_CMD = (
    "ssh -o BatchMode=yes -o StrictHostKeyChecking=no "
    "-o ControlMaster=auto -o ControlPath=/tmp/peregrine_ssh_%r@%h:%p "
    "-o ControlPersist=60s"
)


def is_local_node(host: str) -> bool:
  """Returns True if `host` refers to the local machine."""
  normalized = host.strip().lower()
  if normalized in {"localhost", "127.0.0.1", "::1", "[::1]"}:
    return True
  local_names = {socket.gethostname().lower(), socket.getfqdn().lower()}
  return normalized in local_names


def _find_prebuilt_binary() -> str | None:
  """Searches runfiles and blaze-bin/bazel-bin for an existing `:node` binary."""
  candidates: list[str] = []
  # 1. Sibling `../node/node` directory (works inside Blaze/Bazel runfiles tree)
  here = os.path.dirname(os.path.abspath(__file__))
  candidates.append(os.path.normpath(os.path.join(here, "..", "node", "node")))

  # 2. Runfiles environment variables.
  runfiles_dir = os.environ.get("RUNFILES_DIR") or os.environ.get("TEST_SRCDIR")
  if runfiles_dir:
    for rel in (
        "google3/third_party/peregrine/test/cluster/node/node",
        "_main/peregrine/test/cluster/node/node",
        "peregrine/test/cluster/node/node",
    ):
      candidates.append(os.path.join(runfiles_dir, rel))

  # 3. Workspace blaze-bin / bazel-bin outputs.
  cwd = os.environ.get("BUILD_WORKING_DIRECTORY", os.getcwd())
  candidates.extend([
      os.path.join(
          cwd, "blaze-bin/third_party/peregrine/test/cluster/node/node"
      ),
      os.path.join(cwd, "bazel-bin/peregrine/test/cluster/node/node"),
  ])

  for path in candidates:
    if os.path.isfile(path) and os.access(path, os.X_OK):
      resolved = os.path.abspath(path)
      logging.info("  Found pre-built :node binary: %s", resolved)
      return resolved
  return None


def locate_node_binary(explicit_path: str = "") -> str:
  """Locates the `:node` binary via `--node_binary`, runfiles, or bin dir."""
  if explicit_path:
    abs_path = os.path.abspath(explicit_path)
    if not os.path.isfile(abs_path) or not os.access(abs_path, os.X_OK):
      raise FileNotFoundError(
          "Specified --node_binary does not exist or is not executable:"
          f" {abs_path}"
      )
    logging.info("  Using explicit --node_binary: %s", abs_path)
    return abs_path

  found = _find_prebuilt_binary()
  if found is not None:
    return found

  raise FileNotFoundError(
      "Could not locate :node binary in runfiles or bin directory; "
      "build it first or pass --node_binary."
  )


def raise_local_nofile_limit(target_nofile: int = 1048576) -> None:
  """Raises local soft RLIMIT_NOFILE up to min(target_nofile, hard_limit)."""
  try:
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    desired = (
        target_nofile
        if hard == resource.RLIM_INFINITY
        else min(target_nofile, hard)
    )
    if soft < desired:
      resource.setrlimit(resource.RLIMIT_NOFILE, (desired, hard))
  except (OSError, ValueError):
    pass


class NodeExecutor:
  """Executes commands and stages/runs `:node` locally or via `--ssh_cmd` pipe."""

  def __init__(
      self,
      ssh_cmd: str = DEFAULT_SSH_CMD,
      remote_dir: str = "/tmp",
      session_id: str | None = None,
  ):
    self._ssh_argv = shlex.split(ssh_cmd)
    self._remote_dir = remote_dir.rstrip("/") or "/tmp"
    self._session_id = session_id or uuid.uuid4().hex[:8]
    self._staged_remotes: dict[str, str] = {}

  def _build_ssh_cmd(self, host: str, remote_shell_cmd: str) -> list[str]:
    if self._ssh_argv and os.path.basename(self._ssh_argv[0]) == "gcloud":
      return [*self._ssh_argv, host, f"--command={remote_shell_cmd}"]
    return [*self._ssh_argv, host, remote_shell_cmd]

  def run_shell(self, host: str, shell_cmd: str, timeout: float = 30.0) -> str:
    """Runs a shell command on `host` and returns stdout."""
    if is_local_node(host):
      res = subprocess.run(
          ["bash", "-c", shell_cmd],
          capture_output=True,
          text=True,
          timeout=timeout,
          check=True,
      )
      return res.stdout
    res = subprocess.run(
        self._build_ssh_cmd(host, shell_cmd),
        capture_output=True,
        text=True,
        timeout=timeout,
        check=True,
    )
    return res.stdout

  def inspect_node(self, host: str) -> preflight.NodeSysInfo:
    """Inspects kernel sysctl and ulimit values on `host`."""
    if is_local_node(host):
      return preflight.inspect_local_node(host)
    out = self.run_shell(host, preflight.REMOTE_PROBE_SCRIPT)
    return preflight.parse_probe_output(host, out)

  def resolve_bind_ip(self, host: str, explicit_ip: str = "") -> str:
    """Resolves the Peregrine bind IP for `host` (supports `user@host` or `user@ip`)."""
    if explicit_ip:
      return explicit_ip
    if is_local_node(host):
      return "127.0.0.1"
    host_only = host.split("@", 1)[-1].strip()
    try:
      ipaddress.ip_address(host_only)
      return host_only
    except ValueError:
      pass
    out = self.run_shell(host, "hostname -I | awk '{print $1}'").strip()
    if not out:
      raise RuntimeError(f"Failed to resolve bind IP on remote host '{host}'")
    return out

  def _stage_to_single_remote(self, host: str, local_binary_path: str) -> str:
    """Stages `local_binary_path` to `host` via SSH stdin pipe."""
    remote_path = f"{self._remote_dir}/peregrine_node_{self._session_id}"
    remote_cmd = (
        f"cat > {shlex.quote(remote_path)} && "
        f"chmod +x {shlex.quote(remote_path)}"
    )
    ssh_cmd = self._build_ssh_cmd(host, remote_cmd)
    logging.info("  Staging command for %s: %s", host, shlex.join(ssh_cmd))
    with open(local_binary_path, "rb") as src_file:
      proc = subprocess.Popen(
          ssh_cmd,
          stdin=subprocess.PIPE,
          stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
      )
      assert proc.stdin is not None
      copy_err: OSError | None = None
      try:
        shutil.copyfileobj(src_file, proc.stdin, length=1024 * 1024)
      except OSError as e:
        copy_err = e
      _, stderr_bytes = proc.communicate(timeout=120)
      if proc.returncode != 0 or copy_err is not None:
        err_msg = stderr_bytes.decode("utf-8", errors="replace").strip()
        if copy_err is not None:
          err_msg = (
              f"{err_msg} (copyfileobj error: {copy_err})"
              if err_msg
              else f"copyfileobj error: {copy_err}"
          )
        raise RuntimeError(
            f"Failed to stage binary to '{host}' via SSH pipe: {err_msg}"
        )
    return remote_path

  def stage_binary(
      self, hosts: Sequence[str], local_binary_path: str
  ) -> dict[str, str]:
    """Stages `local_binary_path` to all unique hosts (zero-copy on localhost)."""
    unique_hosts = list(dict.fromkeys(hosts))
    host_to_bin: dict[str, str] = {}
    remote_hosts: list[str] = []

    for h in unique_hosts:
      if is_local_node(h):
        host_to_bin[h] = local_binary_path
      else:
        remote_hosts.append(h)

    if remote_hosts:
      logging.info(
          "  Staging :node binary via SSH pipe to %d remote host(s): %s...",
          len(remote_hosts),
          ", ".join(remote_hosts),
      )
      with futures.ThreadPoolExecutor(
          max_workers=min(16, len(remote_hosts))
      ) as pool:
        future_map = {
            pool.submit(self._stage_to_single_remote, h, local_binary_path): h
            for h in remote_hosts
        }
        for fut in futures.as_completed(future_map):
          h = future_map[fut]
          remote_path = fut.result()
          host_to_bin[h] = remote_path
          self._staged_remotes[h] = remote_path
          logging.info("  Staged :node to %s:%s", h, remote_path)

    return host_to_bin

  def spawn_node(
      self,
      host: str,
      binary_path: str,
      args: Sequence[str],
      target_nofile: int = 1048576,
  ) -> subprocess.Popen[str]:
    """Spawns a `:node` process on `host` with line-buffered stdin/stdout/stderr."""
    if is_local_node(host):
      raise_local_nofile_limit(target_nofile)
      return subprocess.Popen(
          [binary_path, *args],
          stdin=subprocess.PIPE,
          stdout=subprocess.PIPE,
          stderr=subprocess.PIPE,
          text=True,
          bufsize=1,
      )

    quoted_cmd = " ".join(shlex.quote(x) for x in [binary_path, *args])
    remote_cmd = (
        "echo $$ 2>/dev/null >/dev/cgroup/memory/cgroup.procs || true; "
        f"ulimit -n {int(target_nofile)} 2>/dev/null || true; exec {quoted_cmd}"
    )
    return subprocess.Popen(
        self._build_ssh_cmd(host, remote_cmd),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

  def cleanup(self) -> None:
    """Removes any remote staged binaries."""
    if self._staged_remotes:
      logging.info(
          "Cleaning up staged :node binary on %d remote host(s)...",
          len(self._staged_remotes),
      )
    for host, remote_path in list(self._staged_remotes.items()):
      try:
        subprocess.run(
            self._build_ssh_cmd(host, f"rm -f {shlex.quote(remote_path)}"),
            capture_output=True,
            timeout=15,
            check=False,
        )
      except OSError:
        pass
    self._staged_remotes.clear()
