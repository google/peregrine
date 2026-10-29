"""Local subprocess and SSH-pipe node execution for Peregrine cluster tests."""

from collections.abc import Sequence
from concurrent import futures
import io
import ipaddress
import os
import resource
import shlex
import shutil
import socket
import subprocess
import tarfile
import tempfile
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
    self._trace_dirs: dict[str, str] = {}

  def _build_ssh_cmd(self, host: str, remote_shell_cmd: str) -> list[str]:
    if self._ssh_argv and os.path.basename(self._ssh_argv[0]) == "gcloud":
      return [*self._ssh_argv, host, f"--command={remote_shell_cmd}"]
    return [*self._ssh_argv, host, remote_shell_cmd]

  def run_shell(self, host: str, shell_cmd: str, timeout: float = 30.0) -> str:
    """Runs a shell command on `host` and returns stdout."""
    if is_local_node(host):
      res = subprocess.run(
          ["bash", "-c", shell_cmd],
          stdin=subprocess.DEVNULL,
          capture_output=True,
          text=True,
          timeout=timeout,
          check=True,
      )
      return res.stdout
    res = subprocess.run(
        self._build_ssh_cmd(host, shell_cmd),
        stdin=subprocess.DEVNULL,
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
      return explicit_ip.strip().strip("[]")
    if is_local_node(host):
      return "::1" if ":" in host else "127.0.0.1"
    host_only = host.split("@", 1)[-1].strip().strip("[]")
    try:
      ipaddress.ip_address(host_only)
      return host_only
    except ValueError:
      pass
    out = self.run_shell(
        host,
        "(hostname -I 2>/dev/null || ip -o addr show scope global"
        " 2>/dev/null | awk '{split($4, a, \"/\"); print a[1]}') | awk"
        " '{print $1; exit}'",
    ).strip()
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

  def prepare_trace_dir(self, host: str) -> str:
    """Returns a session-isolated trace directory on `host`."""
    existing = self._trace_dirs.get(host)
    if existing is not None:
      return existing
    if is_local_node(host) and self._remote_dir == "/tmp":
      base_dir = tempfile.gettempdir().rstrip("/") or "/tmp"
    else:
      base_dir = self._remote_dir
    trace_dir = f"{base_dir}/peregrine_traces_{self._session_id}"
    self._trace_dirs[host] = trace_dir
    return trace_dir

  def _collect_from_single_remote(
      self, host: str, remote_trace_dir: str, local_traces_dir: str
  ) -> list[str]:
    """Streams `.json` trace files from `remote_trace_dir` on `host`."""
    quoted_dir = shlex.quote(remote_trace_dir)
    remote_cmd = (
        f"if [ -d {quoted_dir} ]; then tar -czf - -C {quoted_dir} .; fi"
    )
    res = subprocess.run(
        self._build_ssh_cmd(host, remote_cmd),
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=120,
        check=False,
    )
    if res.returncode != 0:
      err_msg = res.stderr.decode("utf-8", errors="replace").strip()
      raise RuntimeError(
          f"Failed to collect traces from '{host}:{remote_trace_dir}':"
          f" {err_msg}"
      )
    if not res.stdout:
      return []
    collected: list[str] = []
    with tarfile.open(fileobj=io.BytesIO(res.stdout), mode="r:gz") as tf:
      for member in tf.getmembers():
        if not member.isfile() or not member.name.endswith(".json"):
          continue
        basename = os.path.basename(member.name)
        if not basename:
          continue
        extracted = tf.extractfile(member)
        if extracted is None:
          continue
        dst_path = os.path.join(local_traces_dir, basename)
        with open(dst_path, "wb") as dst_file:
          shutil.copyfileobj(extracted, dst_file)
        collected.append(dst_path)
    return collected

  def collect_traces(
      self, hosts: Sequence[str], local_traces_dir: str
  ) -> list[str]:
    """Copies `.json` trace files from `hosts` into `local_traces_dir`."""
    os.makedirs(local_traces_dir, exist_ok=True)
    collected: list[str] = []
    seen_local_dirs: set[str] = set()
    remote_targets: dict[str, str] = {}

    for h in dict.fromkeys(hosts):
      trace_dir = self._trace_dirs.get(h)
      if not trace_dir:
        continue
      if is_local_node(h):
        if trace_dir in seen_local_dirs or not os.path.isdir(trace_dir):
          continue
        seen_local_dirs.add(trace_dir)
        for name in sorted(os.listdir(trace_dir)):
          if not name.endswith(".json"):
            continue
          src_path = os.path.join(trace_dir, name)
          if not os.path.isfile(src_path):
            continue
          dst_path = os.path.join(local_traces_dir, name)
          shutil.copy2(src_path, dst_path)
          collected.append(dst_path)
      else:
        remote_targets[h] = trace_dir

    if remote_targets:
      with futures.ThreadPoolExecutor(
          max_workers=min(16, len(remote_targets))
      ) as pool:
        future_map = {
            pool.submit(
                self._collect_from_single_remote, h, rdir, local_traces_dir
            ): h
            for h, rdir in remote_targets.items()
        }
        for fut in futures.as_completed(future_map):
          collected.extend(fut.result())

    return sorted(collected)

  def cleanup(self) -> None:
    """Terminates any lingering remote processes and removes staged artifacts."""
    remote_hosts = list(
        dict.fromkeys([
            *self._staged_remotes.keys(),
            *(h for h in self._trace_dirs if not is_local_node(h)),
        ])
    )
    if remote_hosts:
      logging.info(
          "Cleaning up staged :node artifacts on %d remote host(s)...",
          len(remote_hosts),
      )
    for host in remote_hosts:
      cmd_parts: list[str] = []
      remote_path = self._staged_remotes.get(host)
      if remote_path is not None:
        pattern = shlex.quote(f"^{remote_path}( |$)")
        quoted_path = shlex.quote(remote_path)
        cmd_parts.append(
            f"pkill -9 -f {pattern} 2>/dev/null || true; rm -f {quoted_path}"
        )
      remote_trace_dir = self._trace_dirs.get(host)
      if remote_trace_dir is not None:
        cmd_parts.append(f"rm -rf {shlex.quote(remote_trace_dir)}")
      if not cmd_parts:
        continue
      try:
        subprocess.run(
            self._build_ssh_cmd(host, "; ".join(cmd_parts)),
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=15,
            check=False,
        )
      except OSError:
        pass

    for host, trace_dir in list(self._trace_dirs.items()):
      if is_local_node(host):
        shutil.rmtree(trace_dir, ignore_errors=True)

    self._staged_remotes.clear()
    self._trace_dirs.clear()
