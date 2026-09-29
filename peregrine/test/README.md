# Integration Tests

- This `/peregrine/test/` folder contains integration tests and benchmarks;
- Unit tests should be put in the `/peregrine/src/` folder together with
  the `.{h,cc}` files they target to test. This way, it is easy to check
  if there is any unit test missing.

| Subdirectory   |           Namespace          |         Purpose            |
| :------------- | :--------------------------- | :------------------------- |
| `benchmark/`   | `peregrine::benchmark`       | Benchmarks across hosts    |
| `integration/` | `peregrine::integration`     | Tests in a single process  |
| `workloads/`   | `peregrine::workloads`       | Shared workload generators |

## Running Integration Tests

Run with default settings (`serial_fixed_write` workload, 1 GiB transfer size,
30s duration):

```bash
$ bazelisk run -c opt //peregrine/test/integration:main
```

Run `serial_fixed_write` with custom transfer size, duration, and connections
per peer:

```bash
$ bazelisk run -c opt //peregrine/test/integration:main -- \
    --workload=serial_fixed_write \
    --xfer_size=67108864 \
    --test_duration=10s \
    --conns_per_peer=4
```

Run `kv_cache` workload:

```bash
$ bazelisk run -c opt //peregrine/test/integration:main -- \
    --workload=kv_cache \
    --num_layers=32 \
    --num_blocks=64 \
    --block_size=1048576
```

### Error Injection

Inject a one-shot mid-payload TCP `EOF` (`SHUT_RDWR`) on chunk 5:

```bash
$ bazelisk run -c opt //peregrine/test/integration:main -- \
    --error_inject=mid_chunk_disconnect \
    --disconnect_mode=eof \
    --disconnect_point=mid_payload \
    --disconnect_on_chunk=5
```

Inject an abrupt TCP `RST` mid-header every 10 chunks:

```bash
$ bazelisk run -c opt //peregrine/test/integration:main -- \
    --error_inject=mid_chunk_disconnect \
    --disconnect_mode=rst \
    --disconnect_point=mid_header \
    --disconnect_on_chunk=0 \
    --disconnect_every_n_chunks=10
```