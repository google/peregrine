# Release Plan

## Background

- Peregrine has been developed together with [TPU Sync](https://github.com/google/tpu-sync).
  So this roadmap includes some migration work in its first few releases.

## Versions

### v0.1.0

- Expose a minimal set of TCP socket read/write API.
- Eliminate some redundant code in [TPU Sync](https://github.com/google/tpu-sync) transport.

### v0.2.0

- Use Peregrine Post() API in [TPU Sync](https://github.com/google/tpu-sync), minimizing its transport code.
- TCP socket support with multi-NIC capabilities.

### v0.3.0

- RDMA support with ~zero [TPU Sync](https://github.com/google/tpu-sync) code change.
