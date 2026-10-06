# Peregrine Cluster Benchmarks (`//peregrine/test/cluster`)

This directory provides a multi-node, multi-instance cluster benchmarking
framework (`namespace peregrine::cluster`) that coordinates:

- **$N$ Client Nodes**, each running **$N_p$ Peregrine `Transport` instances**
  within a single process ($N \times N_p$ total client instances).
- **$M$ Server Nodes**, each running **$M_p$ Peregrine `Transport` instances**
  within a single process ($M \times M_p$ total server instances).

## Subdirectories

| Subdirectory    | Purpose                                                     |
| :-------------- | :---------------------------------------------------------- |
| `node/`         | C++ symmetric `ClusterNode` runtime & `:node` binary        |
| `orchestrator/` | Python multi-node orchestrator & `:bipartite_runner` CLI    |

## Test Workflow

Each `bipartite_runner` session executes the following steps:

1. **Preflight & Staging**: Inspects kernel `sysctl` / `ulimit` limits on all
   hosts (warning if below recommended thresholds) and stages the `:node` binary
   to remote hosts over SSH `stdin`.
2. **Server Initialization**: Spawns $M$ server `:node` processes ($M_p$
   `Transport` instances per node), registers memory buffers, and collects their
   `<endpoint>@<raddr>` targets via `PEREGRINE_NODE_READY`.
3. **Client Initialization**: Spawns $N$ client `:node` processes ($N_p$
   `Transport` instances per node) configured with the server targets and waits
   for `PEREGRINE_NODE_READY`.
4. **Synchronized Workload Execution**: Broadcasts `START` across all nodes so
   client instances concurrently drive transfers (`all_to_all` or `round_robin`)
   and stream periodic `PEREGRINE_METRICS_SAMPLE` updates until emitting
   `PEREGRINE_WORKLOAD_DONE`.
5. **Teardown & Reporting**: Broadcasts `STOP` to collect final
   `PEREGRINE_NODE_DONE` snapshots (`TransportMetrics` and `CpuStats`) from all
   nodes, cleans up remote binaries, and prints the cluster-wide throughput,
   latency percentile, CPU, and transport metrics summary.

## Running the Fan-In TCP Accept Test ($N=1, N_p=1,000, M=1, M_p=1$)

### Localhost (Single Host)

```bash
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=localhost \
    --server_nodes=localhost \
    --clients_per_node=1000 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all
```

### Multi-Node Cluster (Remote Hosts over SSH)

When remote hostnames or IPs are passed via `--client_nodes` and
`--server_nodes` (supporting both `<host>` and `<user>@<host>` syntax),
`bipartite_runner` automatically streams the `:node` binary over `--ssh_cmd`
`stdin` (without requiring `scp` or `sftp-server`):

```bash
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=user@10.0.0.1,user@10.0.0.2 \
    --server_nodes=user@10.0.0.3 \
    --clients_per_node=500 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all \
    --output_json=/tmp/bipartite_results.json
```

### Running Across Virtual Machines (VMs)

When benchmarking across cloud or local VMs where:

- SSH uses a custom identity key, port, jump host, or `gcloud compute ssh`
  (`--ssh_cmd`), or
- The data-plane NIC IPs differ from the SSH management hostnames
  (`--client_ips`, `--server_ips`):

**Standard SSH with custom key/port and separate data-plane IPs:**

```bash
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=ubuntu@vm-client-1,ubuntu@vm-client-2 \
    --server_nodes=ubuntu@vm-server-1 \
    --client_ips=192.168.100.11,192.168.100.12 \
    --server_ips=192.168.100.21 \
    --ssh_cmd="ssh -i ~/.ssh/vm_key -p 2222 -o BatchMode=yes -o StrictHostKeyChecking=no" \
    --clients_per_node=500 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all
```

**Google Cloud Compute Engine VMs (`gcloud compute ssh`):**

```bash
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=user@gce-client-1,user@gce-client-2 \
    --server_nodes=user@gce-server-1 \
    --ssh_cmd="gcloud compute ssh --project=my-project --zone=us-central1-a" \
    --clients_per_node=500 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all
```

### Running in Containers (Docker / Podman / Kubernetes)

Because `NodeExecutor` stages the `:node` binary and exchanges control messages
purely over standard input/output (`[*ssh_cmd, <node>, <remote_shell_cmd>]`),
you can target containers directly—either via SSH inside the container or via
`docker exec -i` / `kubectl exec -i` without running `sshd` inside the
containers.

*(Tip: Start containers with `--ulimit nofile=1048576:1048576` and sufficient
`--sysctl net.core.somaxconn=20000` for high-fan-in tests.)*

**Docker / Podman (`docker exec -i`):**

```bash
# Start two containers on a shared bridge network with raised limits:
$ docker network create peregrine-net
$ for name in pg-server-0 pg-client-0; do
    docker run -d --rm --name "${name}" --network peregrine-net \
      --ulimit nofile=1048576:1048576 \
      --sysctl net.core.somaxconn=20000 \
      ubuntu:24.04 sleep infinity
  done

# Run the cluster benchmark across the containers:
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=pg-client-0 \
    --server_nodes=pg-server-0 \
    --ssh_cmd='sh -c '\''docker exec -i "$0" sh -c "$1"'\''' \
    --clients_per_node=1000 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all
```

**Kubernetes Pods (`kubectl exec -i`):**

```bash
$ bazelisk run -c opt //peregrine/test/cluster/orchestrator:bipartite_runner -- \
    --client_nodes=peregrine-client-0,peregrine-client-1 \
    --server_nodes=peregrine-server-0 \
    --ssh_cmd='sh -c '\''kubectl exec -i "$0" -- sh -c "$1"'\''' \
    --clients_per_node=500 \
    --servers_per_node=1 \
    --transport=tcp \
    --conn=1 \
    --workload=serial_fixed_write \
    --xfer_size=1 \
    --num_xfers=1 \
    --traffic_pattern=all_to_all
```
