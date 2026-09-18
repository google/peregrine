#!/bin/bash

# Runs Peregrine Bazel build and CPU unit tests.

set -euxo pipefail

cd "$(dirname "$0")/.."

# Install required system packages if missing (e.g., libibverbs-dev for RDMA headers/libraries).
if ! dpkg -s libibverbs-dev >/dev/null 2>&1; then
  sudo apt-get update -y
  sudo apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    libibverbs-dev \
    python3 \
    python3-dev \
    wget
fi

# Ensure IPv6 loopback (::1) is enabled in CI environments so dual-stack (AF_INET + AF_INET6)
# socket unit tests can bind to [::1].
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
  sudo sysctl -w net.ipv6.conf.all.disable_ipv6=0 \
    net.ipv6.conf.default.disable_ipv6=0 \
    net.ipv6.conf.lo.disable_ipv6=0 || true
  sudo ip -6 addr add ::1/128 dev lo 2>/dev/null || true
fi

# Ensure the Bazel version from .bazelversion is installed.
BAZEL_VERSION="$(tr -d '[:space:]' < .bazelversion)"
if ! command -v bazel >/dev/null 2>&1 || [[ "$(bazel --version 2>/dev/null | awk '{print $2}')" != "${BAZEL_VERSION}" ]]; then
  BAZEL_BIN_DIR="${HOME}/.local/bin"
  mkdir -p "${BAZEL_BIN_DIR}"
  wget -nv "https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-linux-x86_64" \
    -O "${BAZEL_BIN_DIR}/bazel"
  chmod +x "${BAZEL_BIN_DIR}/bazel"
  export PATH="${BAZEL_BIN_DIR}:${PATH}"
fi

bazel --version

COMMON_ARGS=(
  "--copt=-g0"
  "--host_copt=-g0"
  "--per_file_copt=external/.*@-w"
  "--host_per_file_copt=external/.*@-w"
  "--verbose_failures"
)
if [[ -n "${RUNNER_TEMP:-}" ]]; then
  COMMON_ARGS+=(
    "--repository_cache=${RUNNER_TEMP}/bazel_repo_cache"
    "--disk_cache=${RUNNER_TEMP}/bazel_disk_cache"
  )
fi

# Build all targets in the workspace.
bazel build \
  "${COMMON_ARGS[@]}" \
  -- //...

# Run all unit tests in the workspace (targets tagged "manual" are skipped automatically by Bazel).
bazel test \
  "${COMMON_ARGS[@]}" \
  --test_output=errors \
  -- //...

